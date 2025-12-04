#pragma once

#include <string>
#include <chrono>

namespace Config {
    constexpr int UDP_PORT = 31415;
    constexpr const char* BIND_ADDRESS = "0.0.0.0";
    constexpr const char* LOG_FILENAME = "keyboard_log.txt";
    constexpr std::chrono::minutes INACTIVITY_THRESHOLD{15};
    constexpr std::chrono::minutes SUSPEND_DETECTION_THRESHOLD{2};
    constexpr size_t BUFFER_SIZE = 4096;
    
    // Service configuration
    namespace Service {
        constexpr const wchar_t* NAME = L"UdpKeyboardLogger";
        constexpr const wchar_t* DISPLAY_NAME = L"UDP Keyboard Logger Service";
        constexpr const wchar_t* DESCRIPTION = L"Logs keyboard input received via UDP protocol to file with daily rotation";
    }

    // Driver configuration defaults
    namespace Driver {
        constexpr const wchar_t* DEFAULT_REMOTE_IP = L"127.0.0.1";
        constexpr const wchar_t* DEFAULT_REMOTE_PORT = L"31415";
    }
}