#pragma once

#include <string>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <mutex>

class FileLogger {
public:
    FileLogger(const std::string& filename);
    ~FileLogger();
    
    void log(const std::string& message);
    void rotateFileIfNeeded();
    bool isNewDay() const;
    void writeHeader(const std::string& listenerAddress, int listenerPort, const std::string& clientAddress = "");
    void writeFooter();
    
private:
    std::string getCurrentDateString() const;
    std::string getCurrentTimeString() const;
    std::string getCurrentDateTimeString() const;
    std::string generateFilename() const;
    
    std::ofstream logFile_;
    std::string baseFilename_;
    std::chrono::year_month_day lastLogDate_;
    std::chrono::system_clock::time_point sessionStartTime_;
    std::mutex fileMutex_;
};