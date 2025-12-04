#include "SystemStatus.h"
#include "UdpServiceManager.h"
#include "DriverInstaller.h"
#include "DebugConfig.h"
#include <Windows.h>
#include <iostream>

std::vector<ComponentStatus> SystemStatus::CheckAllComponents() {
    std::vector<ComponentStatus> components;
    
    ComponentStatus service;
    service.name = L"Windows Service";
    service.status = UdpServiceManager::IsServiceRunning();
    service.details = service.status ? L"RUNNING (UdpKeyboardLogger)" : L"STOPPED (UdpKeyboardLogger)";
    service.fixHint = service.status ? L"" : L"Run 'UdpLogger install' to start";
    components.push_back(service);
    
    ComponentStatus driver;
    driver.name = L"Kernel Driver";
    driver.status = CheckDriverStatus();
    driver.details = driver.status ? L"LOADED (kvckbd.sys)" : L"STOPPED (kvckbd.sys)";
    driver.fixHint = driver.status ? L"" : L"Run 'UdpLogger install' to start";
    components.push_back(driver);
    
    ComponentStatus files;
    files.name = L"Driver File";
    files.status = CheckDriverFile();
    files.details = files.status ? L"Present in DriverStore" : L"Missing driver file";
    files.fixHint = files.status ? L"" : L"Run 'UdpLogger install' to restore";
    components.push_back(files);
    
    return components;
}

bool SystemStatus::CheckDriverStatus() {
    return UdpServiceManager::IsDriverServiceRunning();
}

bool SystemStatus::CheckDriverFile() {
    std::wstring driverStorePath = DriverInstaller::GetDriverStorePath();
    std::wstring sysPath = driverStorePath + L"\\kvckbd.sys";
    
    DWORD attrs = GetFileAttributesW(sysPath.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES);
}