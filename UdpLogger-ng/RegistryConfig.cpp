#include "RegistryConfig.h"
#include "DebugConfig.h"
#include <iostream>

bool RegistryConfig::WriteDriverConfig(
    const std::wstring& remoteIP,
    const std::wstring& remotePort
) noexcept {
    
    DEBUG_LOG(L"Writing driver config to registry: " << remoteIP << L":" << remotePort);

    HKEY hKey = nullptr;
    DWORD disposition = 0;

    // Create or open Parameters key
    LONG result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        REGISTRY_PATH,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &hKey,
        &disposition
    );

    if (result != ERROR_SUCCESS) {
        std::wcerr << L"[RegistryConfig] ERROR: Failed to create/open registry key: " 
                   << result << std::endl;
        return false;
    }

    bool success = true;

    // Write RemoteIP
    result = RegSetValueExW(
        hKey,
        VALUE_REMOTE_IP,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(remoteIP.c_str()),
        static_cast<DWORD>((remoteIP.length() + 1) * sizeof(wchar_t))
    );

    if (result != ERROR_SUCCESS) {
        std::wcerr << L"[RegistryConfig] ERROR: Failed to write RemoteIP: " 
                   << result << std::endl;
        success = false;
    }

    // Write RemotePort
    if (success) {
        result = RegSetValueExW(
            hKey,
            VALUE_REMOTE_PORT,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(remotePort.c_str()),
            static_cast<DWORD>((remotePort.length() + 1) * sizeof(wchar_t))
        );

        if (result != ERROR_SUCCESS) {
            std::wcerr << L"[RegistryConfig] ERROR: Failed to write RemotePort: " 
                       << result << std::endl;
            success = false;
        }
    }

    RegCloseKey(hKey);

    if (success) {
        DEBUG_LOG(L"Driver config written successfully");
    }

    return success;
}

bool RegistryConfig::ReadDriverConfig(
    std::wstring& remoteIP,
    std::wstring& remotePort
) noexcept {
    
    DEBUG_LOG(L"Reading driver config from registry");

    HKEY hKey = nullptr;

    // Open Parameters key
    LONG result = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        REGISTRY_PATH,
        0,
        KEY_READ,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        DEBUG_LOG(L"Registry key not found (not configured yet)");
        return false;
    }

    bool success = true;
    wchar_t buffer[256];
    DWORD bufferSize;
    DWORD type;

    // Read RemoteIP
    bufferSize = sizeof(buffer);
    result = RegQueryValueExW(
        hKey,
        VALUE_REMOTE_IP,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(buffer),
        &bufferSize
    );

    if (result == ERROR_SUCCESS && type == REG_SZ) {
        remoteIP = buffer;
        DEBUG_LOG(L"RemoteIP: " << remoteIP);
    } else {
        std::wcerr << L"[RegistryConfig] ERROR: Failed to read RemoteIP: " 
                   << result << std::endl;
        success = false;
    }

    // Read RemotePort
    if (success) {
        bufferSize = sizeof(buffer);
        result = RegQueryValueExW(
            hKey,
            VALUE_REMOTE_PORT,
            nullptr,
            &type,
            reinterpret_cast<BYTE*>(buffer),
            &bufferSize
        );

        if (result == ERROR_SUCCESS && type == REG_SZ) {
            remotePort = buffer;
            DEBUG_LOG(L"RemotePort: " << remotePort);
        } else {
            std::wcerr << L"[RegistryConfig] ERROR: Failed to read RemotePort: " 
                       << result << std::endl;
            success = false;
        }
    }

    RegCloseKey(hKey);
    return success;
}

bool RegistryConfig::DeleteDriverConfig() noexcept {
    DEBUG_LOG(L"Deleting driver config from registry");

    // Delete entire Parameters key
    LONG result = RegDeleteTreeW(
        HKEY_LOCAL_MACHINE,
        REGISTRY_PATH
    );

    if (result == ERROR_SUCCESS) {
        DEBUG_LOG(L"Driver config deleted successfully");
        return true;
    } else if (result == ERROR_FILE_NOT_FOUND) {
        DEBUG_LOG(L"Driver config already deleted or never existed");
        return true;
    } else {
        std::wcerr << L"[RegistryConfig] ERROR: Failed to delete config: " 
                   << result << std::endl;
        return false;
    }
}