#pragma once

#include <string>
#include <vector>

struct ComponentStatus {
    std::wstring name;
    bool status;
    std::wstring details;
    std::wstring fixHint;
};

class SystemStatus {
public:
    static std::vector<ComponentStatus> CheckAllComponents();
    
private:
    static bool CheckDriverStatus();
    static bool CheckDriverFile();
    static std::wstring GetCheckmark(bool status);
};