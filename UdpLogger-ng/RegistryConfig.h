#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <string>

class RegistryConfig {
public:
    // Write driver configuration to registry
    static bool WriteDriverConfig(
        const std::wstring& remoteIP,
        const std::wstring& remotePort
    ) noexcept;

    // Read driver configuration from registry
    static bool ReadDriverConfig(
        std::wstring& remoteIP,
        std::wstring& remotePort
    ) noexcept;

    // Delete driver configuration from registry
    static bool DeleteDriverConfig() noexcept;

private:
    RegistryConfig() = delete;

    static constexpr const wchar_t* DRIVER_SERVICE_NAME = L"kvckbd";
    static constexpr const wchar_t* REGISTRY_PATH = 
        L"SYSTEM\\CurrentControlSet\\Services\\kvckbd\\Parameters";
    static constexpr const wchar_t* VALUE_REMOTE_IP = L"RemoteIP";
    static constexpr const wchar_t* VALUE_REMOTE_PORT = L"RemotePort";
};