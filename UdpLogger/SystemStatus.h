#pragma once

#include <string>
#include <vector>

// Forward declaration to avoid circular dependency
class UdpServiceManager;

struct ComponentStatus {
    std::wstring name;
    bool status;
    std::wstring details;
    std::wstring fixHint;
};

class SystemStatus {
public:
    static std::vector<ComponentStatus> CheckAllComponents();
    static bool CheckDriverStatus();
    static bool CheckCLSIDHijack();
    static bool CheckTestSigning();
    static bool CheckDriverFiles();
    
private:
    static std::wstring GetCheckmark(bool status);
};