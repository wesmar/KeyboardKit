#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <string>
#include <vector>

class DriverInstaller {
public:
    static bool InstallDriverAndLibrary() noexcept;
    static bool UninstallDriverAndLibrary() noexcept;
    
    static bool InstallRegistryKeys() noexcept;
    static bool UninstallRegistryKeys() noexcept;
    
    static bool EnableTestSigning() noexcept;
    static bool DisableTestSigning() noexcept;
    
    static bool InstallDriverService() noexcept;
    static bool UninstallDriverService() noexcept;
    
    // CHANGE: Make this public so SystemStatus can access it
    static std::wstring GetDriverStorePath() noexcept;
    
private:
    // REMOVED: GetDriverStorePath from private section
    static bool WriteFileToDisk(const std::wstring& path, const std::vector<BYTE>& data) noexcept;
    static bool CreateDirectoryWithTI(const std::wstring& path) noexcept;
};