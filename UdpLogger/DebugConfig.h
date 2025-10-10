// DebugConfig.h
#pragma once

// Debug configuration - set to 1 for detailed logging, 0 for production
#define DEBUG_LOGGING_ENABLED 0

// Debug logging macros that compile out in production
#if DEBUG_LOGGING_ENABLED
    #include <iostream>
    #define DEBUG_LOG(message) std::wcout << L"[DEBUG] " << message << std::endl
    #define DEBUG_LOG_VERBOSE(message) std::wcout << L"[DEBUG_VERBOSE] " << message << std::endl
#else
    #define DEBUG_LOG(message) 
    #define DEBUG_LOG_VERBOSE(message)
#endif