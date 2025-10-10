#include "PathHelper.h"
#include <Windows.h>
#include <ShlObj.h>
#include <iostream>
#include <fstream>

std::filesystem::path PathHelper::GetLogDirectory() {
    // Priority 1: Try %TEMP% (for LocalSystem service)
    wchar_t tempPath[MAX_PATH];
    DWORD tempLen = GetEnvironmentVariableW(L"TEMP", tempPath, MAX_PATH);
    
    if (tempLen > 0 && tempLen < MAX_PATH) {
        std::filesystem::path temp(tempPath);
        if (EnsureDirectoryExists(temp) && IsDirectoryWritable(temp)) {
            std::wcout << L"[PathHelper] Using TEMP directory: " << temp << std::endl;
            return temp;
        }
    }
    
    // Priority 2: Try %TMP% (alternative environment variable)
    DWORD tmpLen = GetEnvironmentVariableW(L"TMP", tempPath, MAX_PATH);
    
    if (tmpLen > 0 && tmpLen < MAX_PATH) {
        std::filesystem::path tmp(tempPath);
        if (EnsureDirectoryExists(tmp) && IsDirectoryWritable(tmp)) {
            std::wcout << L"[PathHelper] Using TMP directory: " << tmp << std::endl;
            return tmp;
        }
    }
    
    // Fallback: Try %USERPROFILE%\Documents
    wchar_t documentsPath[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documentsPath);
    
    if (SUCCEEDED(hr)) {
        std::filesystem::path documents(documentsPath);
        if (EnsureDirectoryExists(documents) && IsDirectoryWritable(documents)) {
            std::wcout << L"[PathHelper] Using Documents directory: " << documents << std::endl;
            return documents;
        }
    }
    
    // Last resort: Current directory
    std::filesystem::path current = std::filesystem::current_path();
    std::wcout << L"[PathHelper] WARNING: Using current directory as fallback: " << current << std::endl;
    return current;
}

std::filesystem::path PathHelper::GetLogFilePath(const std::string& filename) {
    std::filesystem::path logDir = GetLogDirectory();
    return logDir / filename;
}

bool PathHelper::IsDirectoryWritable(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return false;
    }
    
    // Try to create a temporary test file
    std::filesystem::path testFile = path / ".write_test_udplogger.tmp";
    
    try {
        std::ofstream test(testFile, std::ios::out | std::ios::binary);
        if (!test.is_open()) {
            return false;
        }
        test << "test";
        test.close();
        
        // Clean up test file
        std::filesystem::remove(testFile);
        return true;
        
    } catch (...) {
        // Clean up on error
        try {
            if (std::filesystem::exists(testFile)) {
                std::filesystem::remove(testFile);
            }
        } catch (...) {}
        return false;
    }
}

bool PathHelper::EnsureDirectoryExists(const std::filesystem::path& path) {
    try {
        if (std::filesystem::exists(path)) {
            return std::filesystem::is_directory(path);
        }
        
        return std::filesystem::create_directories(path);
        
    } catch (const std::exception& e) {
        std::cerr << "[PathHelper] Failed to create directory: " << e.what() << std::endl;
        return false;
    }
}