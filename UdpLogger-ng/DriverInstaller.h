#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <string>
#include <vector>

class DriverInstaller {
public:
    // Full installation/uninstallation
    static bool InstallDriverAndService() noexcept;
    static bool UninstallDriverAndService() noexcept;
    
    // Driver control
    static bool StartDriverService() noexcept;
    static bool StopDriverService() noexcept;
    
    // Driver path utilities
    static std::wstring GetDriverStorePath() noexcept;
    
private:
    static bool CreateDriverService(const std::wstring& driverPath) noexcept;
    static bool DeleteDriverService() noexcept;
};