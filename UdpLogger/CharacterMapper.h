#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>

// Maps characters from one encoding to another based on INI configuration
// Supports UTF-8 multi-byte sequences (e.g., Polish ą, ć, ę)
class CharacterMapper {
public:
    CharacterMapper();
    
    // Load mapping profile from INI file
    // Returns true if profile loaded successfully, false otherwise
    bool LoadProfile(const std::filesystem::path& iniPath, const std::string& profileName);
    
    // Apply character mapping to input string
    // Processes UTF-8 multi-byte sequences correctly
    std::string Map(const std::string& input) const;
    
    // Check if mapping is enabled
    bool IsEnabled() const noexcept { return enabled_; }
    
private:
    bool enabled_;
    std::unordered_map<std::string, std::string> mapping_;
    
    // Parse INI section into mapping table
    void LoadMappingSection(const std::filesystem::path& iniPath, const std::wstring& section);
    
    // Detect UTF-8 sequence length from first byte
    static size_t GetUtf8SequenceLength(unsigned char firstByte) noexcept;
};