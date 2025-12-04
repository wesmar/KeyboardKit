#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <string>
#include <filesystem>

class PathHelper {
public:
    // Get the best available directory for log files
    // Priority: %TEMP% -> %USERPROFILE%\Documents
    static std::filesystem::path GetLogDirectory();
    
    // Get full path for log file with automatic directory resolution
    static std::filesystem::path GetLogFilePath(const std::string& filename);
    
    // Check if directory is writable
    static bool IsDirectoryWritable(const std::filesystem::path& path);
    
    // Create directory if it doesn't exist
    static bool EnsureDirectoryExists(const std::filesystem::path& path);

private:
    PathHelper() = delete;
};