#include "UdpServiceManager.h"
#include "UdpListener.h"
#include "FileLogger.h"
#include "config.h"
#include "PathHelper.h"
#include "DebugConfig.h"
#include <iostream>
#include <memory>
#include <chrono>

// Service static members
SERVICE_STATUS_HANDLE UdpServiceManager::s_serviceStatusHandle = nullptr;
SERVICE_STATUS UdpServiceManager::s_serviceStatus = {};
HANDLE UdpServiceManager::s_serviceStopEvent = nullptr;
volatile bool UdpServiceManager::s_serviceRunning = false;

// Dynamic API pointers
decltype(&::OpenServiceW) UdpServiceManager::g_pOpenServiceW = nullptr;
decltype(&::CreateServiceW) UdpServiceManager::g_pCreateServiceW = nullptr;
decltype(&::DeleteService) UdpServiceManager::g_pDeleteService = nullptr;
decltype(&::StartServiceW) UdpServiceManager::g_pStartServiceW = nullptr;
decltype(&::ControlService) UdpServiceManager::g_pControlService = nullptr;
decltype(&::QueryServiceStatus) UdpServiceManager::g_pQueryServiceStatus = nullptr;

// Global service components
static std::unique_ptr<FileLogger> g_serviceLogger = nullptr;
static std::unique_ptr<UdpListener> g_udpListener = nullptr;
static std::chrono::steady_clock::time_point g_lastActivity;
static bool g_headerWritten = false;
static std::string g_firstClientAddress;

bool UdpServiceManager::InitDynamicAPIs() noexcept {
    static bool initialized = false;
    if (initialized) return true;
    
    HMODULE hAdvapi32 = GetModuleHandleW(L"advapi32.dll");
    if (!hAdvapi32) {
        hAdvapi32 = LoadLibraryW(L"advapi32.dll");
        if (!hAdvapi32) return false;
    }
    
    g_pOpenServiceW = reinterpret_cast<decltype(g_pOpenServiceW)>(GetProcAddress(hAdvapi32, "OpenServiceW"));
    g_pCreateServiceW = reinterpret_cast<decltype(g_pCreateServiceW)>(GetProcAddress(hAdvapi32, "CreateServiceW"));
    g_pDeleteService = reinterpret_cast<decltype(g_pDeleteService)>(GetProcAddress(hAdvapi32, "DeleteService"));
    g_pStartServiceW = reinterpret_cast<decltype(g_pStartServiceW)>(GetProcAddress(hAdvapi32, "StartServiceW"));
    g_pControlService = reinterpret_cast<decltype(g_pControlService)>(GetProcAddress(hAdvapi32, "ControlService"));
    g_pQueryServiceStatus = reinterpret_cast<decltype(g_pQueryServiceStatus)>(GetProcAddress(hAdvapi32, "QueryServiceStatus"));
    
    initialized = (g_pOpenServiceW && g_pCreateServiceW && g_pDeleteService && 
                   g_pStartServiceW && g_pControlService && g_pQueryServiceStatus);
    
    return initialized;
}

bool UdpServiceManager::InstallService(const std::wstring& exePath) noexcept {
    if (!InitDynamicAPIs()) {
        DEBUG_LOG(L"Failed to initialize service APIs");
        return false;
    }
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) {
        DEBUG_LOG(L"Failed to open Service Control Manager: " << GetLastError());
        return false;
    }
    
    // Build service command line with --service parameter
    std::wstring servicePath = L"\"" + exePath + L"\" --service";
    
    SC_HANDLE hService = g_pCreateServiceW(
        hSCM,
        ServiceConstants::SERVICE_NAME,
        ServiceConstants::SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        servicePath.c_str(),
        nullptr, nullptr, nullptr,
        nullptr, // LocalSystem account
        nullptr
    );
    
    if (!hService) {
        DWORD error = GetLastError();
        CloseServiceHandle(hSCM);
        
        if (error == ERROR_SERVICE_EXISTS) {
            DEBUG_LOG(L"Service already exists, attempting to update configuration...");
            
            hService = g_pOpenServiceW(hSCM, ServiceConstants::SERVICE_NAME, SERVICE_CHANGE_CONFIG);
            if (hService) {
                BOOL success = ChangeServiceConfigW(
                    hService,
                    SERVICE_WIN32_OWN_PROCESS,
                    SERVICE_AUTO_START,
                    SERVICE_ERROR_NORMAL,
                    servicePath.c_str(),
                    nullptr, nullptr, nullptr, nullptr, nullptr,
                    ServiceConstants::SERVICE_DISPLAY_NAME
                );
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCM);
                
                if (success) {
                    DEBUG_LOG(L"Service configuration updated successfully");
                    return true;
                } else {
                    DEBUG_LOG(L"Failed to update service configuration: " << GetLastError());
                    return false;
                }
            }
            return false;
        }
        
        DEBUG_LOG(L"Failed to create service: " << error);
        return false;
    }
    
    // Set service description
    SERVICE_DESCRIPTIONW serviceDesc = {};
    serviceDesc.lpDescription = const_cast<wchar_t*>(ServiceConstants::SERVICE_DESCRIPTION);
    ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &serviceDesc);
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    DEBUG_LOG(L"Service '" << ServiceConstants::SERVICE_DISPLAY_NAME << L"' installed successfully");
    
    // Attempt to start the service
    if (StartServiceProcess()) {
        DEBUG_LOG(L"Service started successfully");
    } else {
        DEBUG_LOG(L"Service installed but failed to start automatically");
    }
    
    return true;
}

bool UdpServiceManager::UninstallService() noexcept {
    if (!InitDynamicAPIs()) {
        DEBUG_LOG(L"Failed to initialize service APIs");
        return false;
    }
    
    // First try to stop the service
    StopServiceProcess();
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        DEBUG_LOG(L"Failed to open Service Control Manager: " << GetLastError());
        return false;
    }
    
    SC_HANDLE hService = g_pOpenServiceW(hSCM, ServiceConstants::SERVICE_NAME, DELETE);
    if (!hService) {
        DWORD error = GetLastError();
        CloseServiceHandle(hSCM);
        
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            DEBUG_LOG(L"Service does not exist");
            return true;
        }
        
        DEBUG_LOG(L"Failed to open service for deletion: " << error);
        return false;
    }
    
    BOOL success = g_pDeleteService(hService);
    DWORD error = GetLastError();
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    if (!success) {
        if (error == ERROR_SERVICE_MARKED_FOR_DELETE) {
            DEBUG_LOG(L"Service marked for deletion (will be removed after next reboot)");
            return true;
        }
        DEBUG_LOG(L"Failed to delete service: " << error);
        return false;
    }
    
    DEBUG_LOG(L"Service '" << ServiceConstants::SERVICE_DISPLAY_NAME << L"' uninstalled successfully");
    return true;
}

bool UdpServiceManager::StartServiceProcess() noexcept {
    if (!InitDynamicAPIs()) return false;
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    
    SC_HANDLE hService = g_pOpenServiceW(hSCM, ServiceConstants::SERVICE_NAME, SERVICE_START);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    BOOL success = g_pStartServiceW(hService, 0, nullptr);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    return success || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
}

bool UdpServiceManager::StopServiceProcess() noexcept {
    if (!InitDynamicAPIs()) return false;
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    
    SC_HANDLE hService = g_pOpenServiceW(hSCM, ServiceConstants::SERVICE_NAME, SERVICE_STOP);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    SERVICE_STATUS status;
    BOOL success = g_pControlService(hService, SERVICE_CONTROL_STOP, &status);
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    return success || GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
}

bool UdpServiceManager::IsServiceInstalled() noexcept {
    if (!InitDynamicAPIs()) return false;
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    
    SC_HANDLE hService = g_pOpenServiceW(hSCM, ServiceConstants::SERVICE_NAME, SERVICE_QUERY_STATUS);
    bool installed = (hService != nullptr);
    
    if (hService) CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    return installed;
}

bool UdpServiceManager::IsServiceRunning() noexcept {
    if (!InitDynamicAPIs()) return false;
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    
    SC_HANDLE hService = g_pOpenServiceW(hSCM, ServiceConstants::SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    SERVICE_STATUS status;
    BOOL success = g_pQueryServiceStatus(hService, &status);
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    return success && (status.dwCurrentState == SERVICE_RUNNING);
}

int UdpServiceManager::RunAsService() noexcept {
    // Service table for dispatcher
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { const_cast<wchar_t*>(ServiceConstants::SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };
    
    // Start service control dispatcher
    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        // If we're not running as service, this will fail
        return 1;
    }
    
    return 0;
}

VOID WINAPI UdpServiceManager::ServiceMain(DWORD argc, LPWSTR* argv) {
    // Register service control handler
    s_serviceStatusHandle = RegisterServiceCtrlHandlerW(ServiceConstants::SERVICE_NAME, ServiceCtrlHandler);
    if (!s_serviceStatusHandle) {
        return;
    }
    
    // Initialize service status
    s_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s_serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    s_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    s_serviceStatus.dwWin32ExitCode = NO_ERROR;
    s_serviceStatus.dwServiceSpecificExitCode = 0;
    s_serviceStatus.dwCheckPoint = 0;
    s_serviceStatus.dwWaitHint = 5000;
    
    SetServiceStatus(SERVICE_START_PENDING, NO_ERROR, 5000);
    
    // Create stop event
    s_serviceStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!s_serviceStopEvent) {
        SetServiceStatus(SERVICE_STOPPED, GetLastError());
        return;
    }
    
    // Set running flag before initializing components
    s_serviceRunning = true;
    
    // Initialize service components
    if (!InitializeServiceComponents()) {
        SetServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        ServiceCleanup();
        return;
    }
    
    // Create worker thread
    HANDLE hWorkerThread = CreateThread(nullptr, 0, ServiceWorkerThread, nullptr, 0, nullptr);
    if (!hWorkerThread) {
        SetServiceStatus(SERVICE_STOPPED, GetLastError());
        ServiceCleanup();
        return;
    }
    
    // Service is now running
    SetServiceStatus(SERVICE_RUNNING);
    
    // Wait for stop signal
    WaitForSingleObject(hWorkerThread, INFINITE);
    CloseHandle(hWorkerThread);
    
    // Cleanup and exit
    ServiceCleanup();
    SetServiceStatus(SERVICE_STOPPED);
}

VOID WINAPI UdpServiceManager::ServiceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            SetServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            s_serviceRunning = false;
            if (s_serviceStopEvent) {
                SetEvent(s_serviceStopEvent);
            }
            break;
            
        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(s_serviceStatus.dwCurrentState);
            break;
            
        default:
            break;
    }
}

DWORD WINAPI UdpServiceManager::ServiceWorkerThread(LPVOID param) {
    // Initialize last activity time
    g_lastActivity = std::chrono::steady_clock::now();
    auto lastFlush = std::chrono::steady_clock::now();
    
    // Main service loop
    while (s_serviceRunning) {
        // Check for inactivity
        auto now = std::chrono::steady_clock::now();
        auto inactiveTime = std::chrono::duration_cast<std::chrono::minutes>(now - g_lastActivity);
        
        if (inactiveTime >= Config::INACTIVITY_THRESHOLD) {
            auto secondsInactive = std::chrono::duration_cast<std::chrono::seconds>(inactiveTime);
            
            static std::chrono::steady_clock::time_point lastInactivityLog;
            if (now - lastInactivityLog >= std::chrono::minutes(5)) {
                if (g_serviceLogger) {
                    std::string inactivityMsg = "*** INACTIVITY: No keyboard input for " + 
                                               std::to_string(secondsInactive.count() / 60) + " minutes ***";
                    g_serviceLogger->log(inactivityMsg);
                }
                lastInactivityLog = now;
            }
        }
        
        // Periodic flush every 15 minutes
        auto timeSinceFlush = std::chrono::duration_cast<std::chrono::minutes>(now - lastFlush);
        if (timeSinceFlush >= std::chrono::minutes(15)) {
            if (g_serviceLogger) {
                auto systemNow = std::chrono::system_clock::now();
                auto currentTime = std::chrono::system_clock::to_time_t(systemNow);
                std::ofstream flushLog("C:\\Temp\\udp_flush_debug.log", std::ios::app);
                flushLog << "Periodic flush at: " << currentTime << std::endl;
            }
            lastFlush = now;
        }
        
        // Wait for stop event with timeout
        DWORD waitResult = WaitForSingleObject(s_serviceStopEvent, 1000);
        
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
    }
    
    return 0;
}

bool UdpServiceManager::SetServiceStatus(DWORD currentState, DWORD exitCode, DWORD waitHint) noexcept {
    static DWORD checkPoint = 1;
    
    s_serviceStatus.dwCurrentState = currentState;
    s_serviceStatus.dwWin32ExitCode = exitCode;
    s_serviceStatus.dwWaitHint = waitHint;
    
    if (currentState == SERVICE_START_PENDING || currentState == SERVICE_STOP_PENDING) {
        s_serviceStatus.dwCheckPoint = checkPoint++;
    } else {
        s_serviceStatus.dwCheckPoint = 0;
    }
    
    return ::SetServiceStatus(s_serviceStatusHandle, &s_serviceStatus) != FALSE;
}

bool UdpServiceManager::InitializeServiceComponents() noexcept {
    try {
        // Get log file path from PathHelper
        std::filesystem::path logPath = PathHelper::GetLogFilePath(Config::LOG_FILENAME);
        
        // Initialize logger
        g_serviceLogger = std::make_unique<FileLogger>(logPath.string());
        
        // Initialize UDP listener
        g_udpListener = std::make_unique<UdpListener>(Config::BIND_ADDRESS, Config::UDP_PORT);
        
        // Set message handler
        g_udpListener->setMessageHandler([](const std::string& message, const std::string& client) {
            g_lastActivity = std::chrono::steady_clock::now();
            
            // Write header on first message
            if (!g_headerWritten) {
                g_firstClientAddress = client;
                g_serviceLogger->writeHeader(Config::BIND_ADDRESS, Config::UDP_PORT, client);
                g_headerWritten = true;
            }
            
            // Log message (without client address - it's in header)
            g_serviceLogger->log(message);
        });
        
        // Start UDP listener
        if (!g_udpListener->start()) {
            return false;
        }
        
        return true;
        
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
}

void UdpServiceManager::ServiceCleanup() noexcept {
    // Stop UDP listener
    if (g_udpListener) {
        g_udpListener->stop();
        g_udpListener.reset();
    }
    
    // Write footer and close logger
    if (g_serviceLogger) {
        g_serviceLogger->writeFooter();
        g_serviceLogger.reset();
    }
    
    // Close stop event
    if (s_serviceStopEvent) {
        CloseHandle(s_serviceStopEvent);
        s_serviceStopEvent = nullptr;
    }
}

bool UdpServiceManager::StartDriverService() noexcept {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    
    SC_HANDLE hService = OpenServiceW(hSCM, L"kvckbd", SERVICE_START);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    BOOL success = StartServiceW(hService, 0, nullptr);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    return success || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
}

bool UdpServiceManager::StopDriverService() noexcept {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    
    SC_HANDLE hService = OpenServiceW(hSCM, L"kvckbd", SERVICE_STOP);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    SERVICE_STATUS status;
    BOOL success = ControlService(hService, SERVICE_CONTROL_STOP, &status);
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    return success || GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
}

bool UdpServiceManager::RestartDriverService() noexcept {
    StopDriverService();
    Sleep(2000); // Wait for service to stop
    return StartDriverService();
}

bool UdpServiceManager::IsDriverServiceRunning() noexcept {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    
    SC_HANDLE hService = OpenServiceW(hSCM, L"kvckbd", SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    SERVICE_STATUS status;
    BOOL success = QueryServiceStatus(hService, &status);
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    return success && (status.dwCurrentState == SERVICE_RUNNING);
}