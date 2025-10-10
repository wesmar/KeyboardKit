// TrustedInstallerExecutor.h
#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <vector>
#include <array>
#include <filesystem>

// Executes operations with TrustedInstaller privileges by impersonating the TrustedInstaller service token
class TrustedInstallerExecutor {
public:
    TrustedInstallerExecutor();
    ~TrustedInstallerExecutor();

    // Delete copy operations (rule of five)
    TrustedInstallerExecutor(const TrustedInstallerExecutor&) = delete;
    TrustedInstallerExecutor& operator=(const TrustedInstallerExecutor&) = delete;
    TrustedInstallerExecutor(TrustedInstallerExecutor&&) noexcept = delete;
    TrustedInstallerExecutor& operator=(TrustedInstallerExecutor&&) noexcept = delete;

    // Command Execution
    [[nodiscard]] bool RunAsTrustedInstaller(std::wstring_view commandLine, bool showWindow = true);
    [[nodiscard]] bool RunAsTrustedInstaller(const std::filesystem::path& executablePath, 
                                           std::wstring_view arguments = L"", 
                                           bool showWindow = true);

    // File Operations
    [[nodiscard]] bool CopyFileAsTrustedInstaller(const std::wstring& source, const std::wstring& destination) noexcept;
    [[nodiscard]] bool WriteFileAsTrustedInstaller(const std::wstring& path, const std::vector<BYTE>& data) noexcept;
    [[nodiscard]] bool DeleteFileAsTrustedInstaller(const std::wstring& path) noexcept;

    // Registry Operations - String Values (REG_SZ/REG_EXPAND_SZ)
    [[nodiscard]] bool ReadRegistryValueAsTrustedInstaller(
        HKEY hKeyRoot, 
        const std::wstring& subKey, 
        const std::wstring& valueName, 
        std::wstring& outValue) noexcept;
    
    [[nodiscard]] bool WriteRegistryValueAsTrustedInstaller(
        HKEY hKeyRoot, 
        const std::wstring& subKey, 
        const std::wstring& valueName, 
        const std::wstring& value) noexcept;

    // Registry Operations - Binary Values (REG_BINARY)
    [[nodiscard]] bool ReadRegistryBinaryAsTrustedInstaller(
        HKEY hKeyRoot, 
        const std::wstring& subKey, 
        const std::wstring& valueName, 
        std::vector<BYTE>& outData) noexcept;
    
    [[nodiscard]] bool WriteRegistryBinaryAsTrustedInstaller(
        HKEY hKeyRoot, 
        const std::wstring& subKey, 
        const std::wstring& valueName, 
        const std::vector<BYTE>& data) noexcept;

    // Registry Operations - DWORD Values (REG_DWORD)
    [[nodiscard]] bool WriteRegistryDwordAsTrustedInstaller(
        HKEY hKeyRoot, 
        const std::wstring& subKey, 
        const std::wstring& valueName, 
        DWORD value) noexcept;

    // Registry Operations - Key Management
    [[nodiscard]] bool CreateRegistryKeyAsTrustedInstaller(HKEY hKeyRoot, const std::wstring& subKey) noexcept;
    [[nodiscard]] bool DeleteRegistryKeyAsTrustedInstaller(HKEY hKeyRoot, const std::wstring& subKey) noexcept;

    // Static Helper Methods
    [[nodiscard]] static bool IsCurrentProcessElevated() noexcept;
    [[nodiscard]] static std::wstring GetCurrentUserName() noexcept;

private:
    // RAII wrapper for HANDLE
    class TokenHandle {
    public:
        TokenHandle() noexcept = default;
        explicit TokenHandle(HANDLE handle) noexcept : handle_(handle) {}
        ~TokenHandle() { reset(); }
        
        TokenHandle(TokenHandle&& other) noexcept : handle_(other.release()) {}
        TokenHandle& operator=(TokenHandle&& other) noexcept;
        
        [[nodiscard]] HANDLE get() const noexcept { return handle_; }
        [[nodiscard]] HANDLE* address() noexcept { return &handle_; }
        [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
        void reset(HANDLE newHandle = nullptr) noexcept;
        HANDLE release() noexcept;
        
    private:
        HANDLE handle_{nullptr};
    };

    // Privileges required for TrustedInstaller operations
    static constexpr std::array<std::wstring_view, 8> REQUIRED_PRIVILEGES = {
        L"SeDebugPrivilege",
        L"SeImpersonatePrivilege",
        L"SeAssignPrimaryTokenPrivilege",
        L"SeTcbPrivilege",
        L"SeBackupPrivilege",
        L"SeRestorePrivilege",
        L"SeTakeOwnershipPrivilege",
        L"SeSecurityPrivilege"
    };

    // Core Implementation - Token and Service Management
    [[nodiscard]] bool EnableAllPrivileges(const TokenHandle& token) const noexcept;
    [[nodiscard]] bool EnablePrivilege(std::wstring_view privilegeName) const noexcept;
    [[nodiscard]] std::optional<DWORD> GetProcessIdByName(std::wstring_view processName) const noexcept;
    [[nodiscard]] TokenHandle GetSystemToken() const noexcept;
    [[nodiscard]] std::optional<DWORD> StartTrustedInstallerService() const noexcept;
    [[nodiscard]] TokenHandle GetTrustedInstallerToken(DWORD trustedInstallerPid) const noexcept;
    [[nodiscard]] bool CreateProcessWithToken(const TokenHandle& token, std::wstring_view commandLine, bool showWindow) const noexcept;

    // File Operations Implementation
    [[nodiscard]] bool CopyFileWithToken(const TokenHandle& token, const std::wstring& source, const std::wstring& destination) const noexcept;
    [[nodiscard]] bool WriteFileWithToken(const TokenHandle& token, const std::wstring& path, const std::vector<BYTE>& data) const noexcept;
    [[nodiscard]] bool DeleteFileWithToken(const TokenHandle& token, const std::wstring& path) const noexcept;

    // Registry Operations Implementation - String Values
    [[nodiscard]] bool ReadRegistryValueWithToken(const TokenHandle& token, HKEY hKeyRoot, const std::wstring& subKey, const std::wstring& valueName, std::wstring& outValue) const noexcept;
    [[nodiscard]] bool WriteRegistryValueWithToken(const TokenHandle& token, HKEY hKeyRoot, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& value) const noexcept;

    // Registry Operations Implementation - Binary Values
    [[nodiscard]] bool ReadRegistryBinaryWithToken(const TokenHandle& token, HKEY hKeyRoot, const std::wstring& subKey, const std::wstring& valueName, std::vector<BYTE>& outData) const noexcept;
    [[nodiscard]] bool WriteRegistryBinaryWithToken(const TokenHandle& token, HKEY hKeyRoot, const std::wstring& subKey, const std::wstring& valueName, const std::vector<BYTE>& data) const noexcept;

    // Registry Operations Implementation - DWORD Values
    [[nodiscard]] bool WriteRegistryDwordWithToken(const TokenHandle& token, HKEY hKeyRoot, const std::wstring& subKey, const std::wstring& valueName, DWORD value) const noexcept;

    // Registry Operations Implementation - Key Management
    [[nodiscard]] bool CreateRegistryKeyWithToken(const TokenHandle& token, HKEY hKeyRoot, const std::wstring& subKey) const noexcept;
    [[nodiscard]] bool DeleteRegistryKeyWithToken(const TokenHandle& token, HKEY hKeyRoot, const std::wstring& subKey) const noexcept;

    bool comInitialized_{false};
};