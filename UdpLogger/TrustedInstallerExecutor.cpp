// TrustedInstallerExecutor.cpp
#include "TrustedInstallerExecutor.h"
#include "DebugConfig.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "wtsapi32.lib")

namespace fs = std::filesystem;

// =============================================================================
// TokenHandle implementation - RAII wrapper for HANDLE
// =============================================================================

TrustedInstallerExecutor::TokenHandle& 
TrustedInstallerExecutor::TokenHandle::operator=(TokenHandle&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

void TrustedInstallerExecutor::TokenHandle::reset(HANDLE newHandle) noexcept {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
    }
    handle_ = newHandle;
}

HANDLE TrustedInstallerExecutor::TokenHandle::release() noexcept {
    HANDLE result = handle_;
    handle_ = nullptr;
    return result;
}

// =============================================================================
// AutoHandle - Simple RAII wrapper for HANDLE
// =============================================================================

class AutoHandle {
public:
    AutoHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~AutoHandle() { if (handle_) CloseHandle(handle_); }
    
    AutoHandle(const AutoHandle&) = delete;
    AutoHandle& operator=(const AutoHandle&) = delete;
    
    AutoHandle(AutoHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    AutoHandle& operator=(AutoHandle&& other) noexcept {
        if (this != &other) {
            reset(other.handle_);
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    operator bool() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return handle_; }
    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = handle;
    }
    
private:
    HANDLE handle_{nullptr};
};

// =============================================================================
// AutoServiceHandle - Simple RAII wrapper for SC_HANDLE
// =============================================================================

class AutoServiceHandle {
public:
    AutoServiceHandle(SC_HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~AutoServiceHandle() { if (handle_) CloseServiceHandle(handle_); }
    
    AutoServiceHandle(const AutoServiceHandle&) = delete;
    AutoServiceHandle& operator=(const AutoServiceHandle&) = delete;
    
    AutoServiceHandle(AutoServiceHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    AutoServiceHandle& operator=(AutoServiceHandle&& other) noexcept {
        if (this != &other) {
            reset(other.handle_);
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    operator bool() const noexcept { return handle_ != nullptr; }
    SC_HANDLE get() const noexcept { return handle_; }
    void reset(SC_HANDLE handle = nullptr) noexcept {
        if (handle_) CloseServiceHandle(handle_);
        handle_ = handle;
    }
    
private:
    SC_HANDLE handle_{nullptr};
};

// =============================================================================
// TrustedInstallerExecutor implementation
// =============================================================================

TrustedInstallerExecutor::TrustedInstallerExecutor() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    comInitialized_ = SUCCEEDED(hr);
}

TrustedInstallerExecutor::~TrustedInstallerExecutor() {
    if (comInitialized_) {
        CoUninitialize();
    }
}

// =============================================================================
// Main Public API Methods
// =============================================================================

bool TrustedInstallerExecutor::RunAsTrustedInstaller(
    std::wstring_view commandLine, bool showWindow) {
    
    if (commandLine.empty()) {
        DEBUG_LOG(L"Error: Empty command line");
        return false;
    }

    DEBUG_LOG(L"Attempting to run as TrustedInstaller: " << commandLine);

    // Enable required privileges for token manipulation
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        DEBUG_LOG(L"Error: Failed to enable required privileges");
        return false;
    }

    // Get SYSTEM token via winlogon process
    auto systemToken = GetSystemToken();
    if (!systemToken) {
        DEBUG_LOG(L"Error: Failed to acquire SYSTEM token");
        return false;
    }

    // Impersonate SYSTEM to start TrustedInstaller service
    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        DEBUG_LOG(L"Error: Failed to impersonate SYSTEM");
        return false;
    }

    // Start TrustedInstaller service and get its token
    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        DEBUG_LOG(L"Error: Failed to start TrustedInstaller service");
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf(); // Always revert impersonation

    if (!tiToken) {
        DEBUG_LOG(L"Error: Failed to acquire TrustedInstaller token");
        return false;
    }

    // Enable all privileges on the token for maximum access
    if (!EnableAllPrivileges(tiToken)) {
        DEBUG_LOG(L"Warning: Failed to enable some privileges on token");
    }

    // Create the process with TrustedInstaller token
    bool success = CreateProcessWithToken(tiToken, commandLine, showWindow);
    
    if (success) {
        DEBUG_LOG(L"Successfully started process with TrustedInstaller privileges");
    } else {
        DEBUG_LOG(L"Error: Failed to create process with TrustedInstaller token");
    }

    return success;
}

bool TrustedInstallerExecutor::RunAsTrustedInstaller(
    const fs::path& executablePath, std::wstring_view arguments, bool showWindow) {
    
    if (!fs::exists(executablePath)) {
        DEBUG_LOG(L"Error: Executable not found: " << executablePath.wstring());
        return false;
    }

    std::wstring commandLine = L"\"" + executablePath.wstring() + L"\"";
    if (!arguments.empty()) {
        commandLine += L" " + std::wstring(arguments);
    }

    return RunAsTrustedInstaller(commandLine, showWindow);
}

// =============================================================================
// New Public API Methods - Direct File Operations
// =============================================================================

bool TrustedInstallerExecutor::CopyFileAsTrustedInstaller(const std::wstring& source, const std::wstring& destination) noexcept {
    // Enable required privileges for token manipulation
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    // Get SYSTEM token via winlogon process
    auto systemToken = GetSystemToken();
    if (!systemToken) {
        return false;
    }

    // Impersonate SYSTEM to start TrustedInstaller service
    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    // Start TrustedInstaller service and get its token
    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) {
        return false;
    }

    // Enable all privileges on the token
    (void)EnableAllPrivileges(tiToken);

    // Perform file copy using TrustedInstaller token
    bool success = CopyFileWithToken(tiToken, source, destination);
    
    return success;
}

bool TrustedInstallerExecutor::WriteFileAsTrustedInstaller(const std::wstring& path, const std::vector<BYTE>& data) noexcept {
    // Enable required privileges for token manipulation
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    // Get SYSTEM token via winlogon process
    auto systemToken = GetSystemToken();
    if (!systemToken) {
        return false;
    }

    // Impersonate SYSTEM to start TrustedInstaller service
    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    // Start TrustedInstaller service and get its token
    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) {
        return false;
    }

    // Enable all privileges on the token
    (void)EnableAllPrivileges(tiToken);

    // Perform direct file write using TrustedInstaller token
    bool success = WriteFileWithToken(tiToken, path, data);
    
    return success;
}

bool TrustedInstallerExecutor::DeleteFileAsTrustedInstaller(const std::wstring& path) noexcept {
    // Enable required privileges for token manipulation
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    // Get SYSTEM token via winlogon process
    auto systemToken = GetSystemToken();
    if (!systemToken) {
        return false;
    }

    // Impersonate SYSTEM to start TrustedInstaller service
    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    // Start TrustedInstaller service and get its token
    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) {
        return false;
    }

    // Enable all privileges on the token
    (void)EnableAllPrivileges(tiToken);

    // Perform file deletion using TrustedInstaller token
    bool success = DeleteFileWithToken(tiToken, path);
    
    return success;
}

// =============================================================================
// Private Implementation Methods - Core Functionality
// =============================================================================

bool TrustedInstallerExecutor::EnableAllPrivileges(const TokenHandle& token) const noexcept {
    bool allSucceeded = true;

    for (auto privilege : REQUIRED_PRIVILEGES) {
        TOKEN_PRIVILEGES tp{};
        LUID luid;

        if (LookupPrivilegeValueW(nullptr, privilege.data(), &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            
            if (!AdjustTokenPrivileges(token.get(), FALSE, &tp, sizeof(tp), nullptr, nullptr)) {
                allSucceeded = false;
            }
        } else {
            allSucceeded = false;
        }
    }

    return allSucceeded;
}

bool TrustedInstallerExecutor::EnablePrivilege(std::wstring_view privilegeName) const noexcept {
    TokenHandle processToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, 
                         processToken.address())) {
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    LUID luid;

    if (!LookupPrivilegeValueW(nullptr, privilegeName.data(), &luid)) {
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    return AdjustTokenPrivileges(processToken.get(), FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
           GetLastError() == ERROR_SUCCESS;
}

std::optional<DWORD> TrustedInstallerExecutor::GetProcessIdByName(
    std::wstring_view processName) const noexcept {
    
    AutoHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) return std::nullopt;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot.get(), &pe)) {
        do {
            if (std::wstring_view(pe.szExeFile) == processName) {
                return pe.th32ProcessID;
            }
        } while (Process32NextW(snapshot.get(), &pe));
    }

    return std::nullopt;
}

TrustedInstallerExecutor::TokenHandle TrustedInstallerExecutor::GetSystemToken() const noexcept {
    auto winlogonPid = GetProcessIdByName(L"winlogon.exe");
    if (!winlogonPid) return TokenHandle{};

    AutoHandle process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, *winlogonPid));
    if (!process) return TokenHandle{};

    TokenHandle token;
    if (!OpenProcessToken(process.get(), TOKEN_DUPLICATE | TOKEN_QUERY, token.address())) {
        return TokenHandle{};
    }

    TokenHandle duplicatedToken;
    if (!DuplicateTokenEx(token.get(), MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                         TokenImpersonation, duplicatedToken.address())) {
        return TokenHandle{};
    }

    return duplicatedToken;
}

std::optional<DWORD> TrustedInstallerExecutor::StartTrustedInstallerService() const noexcept {
    AutoServiceHandle scManager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scManager) return std::nullopt;

    AutoServiceHandle service(OpenServiceW(scManager.get(), L"TrustedInstaller", 
                                         SERVICE_QUERY_STATUS | SERVICE_START));
    if (!service) return std::nullopt;

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    constexpr DWORD timeout = 3000; // 3 seconds timeout
    const auto startTime = GetTickCount64();

    while (QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                               reinterpret_cast<BYTE*>(&status), sizeof(status), &bytesNeeded)) {
        switch (status.dwCurrentState) {
            case SERVICE_RUNNING:
                return status.dwProcessId;

            case SERVICE_STOPPED:
                if (!StartServiceW(service.get(), 0, nullptr)) {
                    return std::nullopt;
                }
                break;

            case SERVICE_START_PENDING:
            case SERVICE_STOP_PENDING:
                if (GetTickCount64() - startTime > timeout) {
                    return std::nullopt;
                }
                Sleep(status.dwWaitHint ? status.dwWaitHint : 100);
                break;

            default:
                Sleep(100);
                break;
        }
    }

    return std::nullopt;
}

TrustedInstallerExecutor::TokenHandle TrustedInstallerExecutor::GetTrustedInstallerToken(
    DWORD trustedInstallerPid) const noexcept {
    
    AutoHandle process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, trustedInstallerPid));
    if (!process) return TokenHandle{};

    TokenHandle token;
    if (!OpenProcessToken(process.get(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, 
                         token.address())) {
        return TokenHandle{};
    }

    TokenHandle duplicatedToken;
    if (!DuplicateTokenEx(token.get(), MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                         TokenImpersonation, duplicatedToken.address())) {
        return TokenHandle{};
    }

    return duplicatedToken;
}

bool TrustedInstallerExecutor::CreateProcessWithToken(const TokenHandle& token,
                                                    std::wstring_view commandLine,
                                                    bool showWindow) const noexcept {
    std::wstring mutableCmd(commandLine);
    
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = showWindow ? SW_SHOW : SW_HIDE;

    PROCESS_INFORMATION pi{};
    
    const DWORD creationFlags = showWindow ? 0 : CREATE_NO_WINDOW;
    
    BOOL success = CreateProcessWithTokenW(token.get(), LOGON_WITH_PROFILE, nullptr,
                                          mutableCmd.data(), creationFlags, nullptr, nullptr,
                                          &si, &pi);

    if (success) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return success;
}

// =============================================================================
// New Private Methods - Direct File Operations with Token
// =============================================================================

bool TrustedInstallerExecutor::CopyFileWithToken(const TokenHandle& token, 
                                               const std::wstring& source, 
                                               const std::wstring& destination) const noexcept {
    bool success = false;

    // Impersonate TrustedInstaller token for file operations
    if (ImpersonateLoggedOnUser(token.get())) {
        // Use native Windows CopyFile API with impersonated token
        if (CopyFileW(source.c_str(), destination.c_str(), FALSE)) {
            success = true;
        } else {
            // Log error for debugging (optional)
            DWORD error = GetLastError();
            DEBUG_LOG_VERBOSE(L"CopyFile failed with error: " << error);
        }
        RevertToSelf(); // Always revert to original context
    }

    return success;
}

bool TrustedInstallerExecutor::WriteFileWithToken(const TokenHandle& token,
                                                const std::wstring& path,
                                                const std::vector<BYTE>& data) const noexcept {
    bool success = false;

    // Impersonate TrustedInstaller token for file operations
    if (ImpersonateLoggedOnUser(token.get())) {
        // Create file with TrustedInstaller privileges
        HANDLE hFile = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            0, // No sharing
            nullptr,
            CREATE_ALWAYS, // Overwrite if exists
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD bytesWritten = 0;
            if (WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr)) {
                success = (bytesWritten == data.size());
            }
            CloseHandle(hFile);
        }
        RevertToSelf(); // Always revert to original context
    }

    return success;
}

bool TrustedInstallerExecutor::DeleteFileWithToken(const TokenHandle& token,
                                                  const std::wstring& path) const noexcept {
    bool success = false;

    // Impersonate TrustedInstaller token for file operations
    if (ImpersonateLoggedOnUser(token.get())) {
        // Use native Windows DeleteFile API with impersonated token
        if (DeleteFileW(path.c_str())) {
            success = true;
        } else {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND) {
                // File already deleted - consider it success
                success = true;
            } else {
                // Log error for debugging (optional)
                DEBUG_LOG_VERBOSE(L"DeleteFile failed with error: " << error);
            }
        }
        RevertToSelf(); // Always revert to original context
    }

    return success;
}

// =============================================================================
// New Public API Methods - Direct Registry Operations
// =============================================================================

bool TrustedInstallerExecutor::ReadRegistryValueAsTrustedInstaller(
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    std::wstring& outValue) noexcept {
    
    // Enable required privileges
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    auto systemToken = GetSystemToken();
    if (!systemToken) return false;

    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) return false;

    (void)EnableAllPrivileges(tiToken);

    return ReadRegistryValueWithToken(tiToken, hKeyRoot, subKey, valueName, outValue);
}

bool TrustedInstallerExecutor::WriteRegistryValueAsTrustedInstaller(
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    const std::wstring& value) noexcept {
    
    // Enable required privileges
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    auto systemToken = GetSystemToken();
    if (!systemToken) return false;

    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) return false;

    (void)EnableAllPrivileges(tiToken);

    return WriteRegistryValueWithToken(tiToken, hKeyRoot, subKey, valueName, value);
}

bool TrustedInstallerExecutor::DeleteRegistryKeyAsTrustedInstaller(
    HKEY hKeyRoot,
    const std::wstring& subKey) noexcept {
    
    // Enable required privileges
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    auto systemToken = GetSystemToken();
    if (!systemToken) return false;

    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) return false;

    (void)EnableAllPrivileges(tiToken);

    return DeleteRegistryKeyWithToken(tiToken, hKeyRoot, subKey);
}

// =============================================================================
// New Private Methods - Registry Operations with Token
// =============================================================================

bool TrustedInstallerExecutor::ReadRegistryValueWithToken(
    const TokenHandle& token,
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    std::wstring& outValue) const noexcept {
    
    bool success = false;

    if (ImpersonateLoggedOnUser(token.get())) {
        HKEY hKey = nullptr;
        
        if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            DWORD dataSize = 0;
            DWORD dataType = 0;
            
            // First call to get size
            if (RegQueryValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(), 
                               nullptr, &dataType, nullptr, &dataSize) == ERROR_SUCCESS) {
                
                if (dataType == REG_SZ || dataType == REG_EXPAND_SZ) {
                    std::vector<wchar_t> buffer(dataSize / sizeof(wchar_t) + 1);
                    
                    if (RegQueryValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(),
                                       nullptr, &dataType, 
                                       reinterpret_cast<BYTE*>(buffer.data()), 
                                       &dataSize) == ERROR_SUCCESS) {
                        outValue = buffer.data();
                        success = true;
                    }
                }
            }
            
            RegCloseKey(hKey);
        }
        
        RevertToSelf();
    }

    return success;
}

bool TrustedInstallerExecutor::WriteRegistryValueWithToken(
    const TokenHandle& token,
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    const std::wstring& value) const noexcept {
    
    bool success = false;

    if (ImpersonateLoggedOnUser(token.get())) {
        HKEY hKey = nullptr;
        
        // Open with write access
        if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_WRITE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            DWORD dataSize = static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t));
            
            if (RegSetValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(),
                             0, REG_EXPAND_SZ, 
                             reinterpret_cast<const BYTE*>(value.c_str()), 
                             dataSize) == ERROR_SUCCESS) {
                success = true;
            }
            
            RegCloseKey(hKey);
        }
        
        RevertToSelf();
    }

    return success;
}

bool TrustedInstallerExecutor::DeleteRegistryKeyWithToken(
    const TokenHandle& token,
    HKEY hKeyRoot,
    const std::wstring& subKey) const noexcept {
    
    bool success = false;

    if (ImpersonateLoggedOnUser(token.get())) {
        if (RegDeleteTreeW(hKeyRoot, subKey.c_str()) == ERROR_SUCCESS) {
            success = true;
        }
        
        RevertToSelf();
    }

    return success;
}

// =============================================================================
// Binary Registry Operations
// =============================================================================

bool TrustedInstallerExecutor::ReadRegistryBinaryAsTrustedInstaller(
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    std::vector<BYTE>& outData) noexcept {
    
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    auto systemToken = GetSystemToken();
    if (!systemToken) return false;

    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) return false;

    (void)EnableAllPrivileges(tiToken);

    return ReadRegistryBinaryWithToken(tiToken, hKeyRoot, subKey, valueName, outData);
}

bool TrustedInstallerExecutor::WriteRegistryBinaryAsTrustedInstaller(
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    const std::vector<BYTE>& data) noexcept {
    
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    auto systemToken = GetSystemToken();
    if (!systemToken) return false;

    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) return false;

    (void)EnableAllPrivileges(tiToken);

    return WriteRegistryBinaryWithToken(tiToken, hKeyRoot, subKey, valueName, data);
}

bool TrustedInstallerExecutor::WriteRegistryDwordAsTrustedInstaller(
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    DWORD value) noexcept {
    
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    auto systemToken = GetSystemToken();
    if (!systemToken) return false;

    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) return false;

    (void)EnableAllPrivileges(tiToken);

    return WriteRegistryDwordWithToken(tiToken, hKeyRoot, subKey, valueName, value);
}

bool TrustedInstallerExecutor::WriteRegistryDwordWithToken(
    const TokenHandle& token,
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    DWORD value) const noexcept {
    
    bool success = false;

    if (ImpersonateLoggedOnUser(token.get())) {
        HKEY hKey = nullptr;
        
        if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_WRITE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            if (RegSetValueExW(hKey, valueName.c_str(),
                             0, REG_DWORD, 
                             reinterpret_cast<const BYTE*>(&value), 
                             sizeof(DWORD)) == ERROR_SUCCESS) {
                success = true;
            }
            
            RegCloseKey(hKey);
        }
        
        RevertToSelf();
    }

    return success;
}

bool TrustedInstallerExecutor::CreateRegistryKeyAsTrustedInstaller(
    HKEY hKeyRoot,
    const std::wstring& subKey) noexcept {
    
    if (!EnablePrivilege(L"SeDebugPrivilege") || !EnablePrivilege(L"SeImpersonatePrivilege")) {
        return false;
    }

    auto systemToken = GetSystemToken();
    if (!systemToken) return false;

    if (!ImpersonateLoggedOnUser(systemToken.get())) {
        return false;
    }

    auto tiPid = StartTrustedInstallerService();
    if (!tiPid) {
        RevertToSelf();
        return false;
    }

    auto tiToken = GetTrustedInstallerToken(*tiPid);
    RevertToSelf();

    if (!tiToken) return false;

    (void)EnableAllPrivileges(tiToken);

    return CreateRegistryKeyWithToken(tiToken, hKeyRoot, subKey);
}

bool TrustedInstallerExecutor::ReadRegistryBinaryWithToken(
    const TokenHandle& token,
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    std::vector<BYTE>& outData) const noexcept {
    
    bool success = false;

    if (ImpersonateLoggedOnUser(token.get())) {
        HKEY hKey = nullptr;
        
        if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            DWORD dataSize = 0;
            DWORD dataType = 0;
            
            // Get size
            if (RegQueryValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(), 
                               nullptr, &dataType, nullptr, &dataSize) == ERROR_SUCCESS) {
                
                if (dataType == REG_BINARY) {
                    outData.resize(dataSize);
                    
                    if (RegQueryValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(),
                                       nullptr, &dataType, outData.data(), &dataSize) == ERROR_SUCCESS) {
                        success = true;
                    }
                }
            }
            
            RegCloseKey(hKey);
        }
        
        RevertToSelf();
    }

    return success;
}

bool TrustedInstallerExecutor::WriteRegistryBinaryWithToken(
    const TokenHandle& token,
    HKEY hKeyRoot,
    const std::wstring& subKey,
    const std::wstring& valueName,
    const std::vector<BYTE>& data) const noexcept {
    
    bool success = false;

    if (ImpersonateLoggedOnUser(token.get())) {
        HKEY hKey = nullptr;
        
        if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_WRITE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            if (RegSetValueExW(hKey, valueName.empty() ? nullptr : valueName.c_str(),
                             0, REG_BINARY, data.data(), static_cast<DWORD>(data.size())) == ERROR_SUCCESS) {
                success = true;
            }
            
            RegCloseKey(hKey);
        }
        
        RevertToSelf();
    }

    return success;
}

bool TrustedInstallerExecutor::CreateRegistryKeyWithToken(
    const TokenHandle& token,
    HKEY hKeyRoot,
    const std::wstring& subKey) const noexcept {
    
    bool success = false;

    if (ImpersonateLoggedOnUser(token.get())) {
        HKEY hKey = nullptr;
        DWORD disposition = 0;
        
        if (RegCreateKeyExW(hKeyRoot, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                           KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKey, &disposition) == ERROR_SUCCESS) {
            success = true;
            RegCloseKey(hKey);
        }
        
        RevertToSelf();
    }

    return success;
}

// =============================================================================
// Static Helper Methods
// =============================================================================

bool TrustedInstallerExecutor::IsCurrentProcessElevated() noexcept {
    TokenHandle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.address())) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    
    return GetTokenInformation(token.get(), TokenElevation, &elevation, size, &size) &&
           elevation.TokenIsElevated;
}

std::wstring TrustedInstallerExecutor::GetCurrentUserName() noexcept {
    TokenHandle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.address())) {
        return L"Unknown";
    }
    
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return L"Unknown";
    }
    
    std::vector<BYTE> buffer(size);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size)) {
        return L"Unknown";
    }
    
    DWORD userNameLen = 0;
    DWORD domainLen = 0;
    auto tokenUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
    
    SID_NAME_USE sidType;
    // First call to get required buffer sizes
    if (!LookupAccountSidW(nullptr, tokenUser->User.Sid, nullptr, &userNameLen, 
                          nullptr, &domainLen, &sidType) && 
        GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        
        std::vector<wchar_t> nameBuffer(userNameLen);
        std::vector<wchar_t> domainBuffer(domainLen);
        
        if (LookupAccountSidW(nullptr, tokenUser->User.Sid, nameBuffer.data(), &userNameLen,
                             domainBuffer.data(), &domainLen, &sidType)) {
            return std::wstring(domainBuffer.data()) + L"\\" + std::wstring(nameBuffer.data());
        }
    }
    
    return L"Unknown";
}