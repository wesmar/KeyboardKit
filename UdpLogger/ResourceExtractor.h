#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <array>

class ResourceExtractor {
public:
    struct ExtractedFile {
        std::wstring filename;
        std::vector<BYTE> data;
        bool isValidPE;
    };

    static std::vector<ExtractedFile> ExtractFilesFromResource(HINSTANCE hInstance, int resourceId) noexcept;

private:
    static constexpr std::array<BYTE, 7> XOR_KEY = { 
        0xA0, 0xE2, 0x80, 0x8B, 0xE2, 0x80, 0x8C 
    };

    static size_t GetIcoSize(const BYTE* data, size_t dataSize) noexcept;
    static void XorDecrypt(BYTE* data, size_t size) noexcept;
    static std::vector<ExtractedFile> ParseTLVPayload(const BYTE* payload, size_t size) noexcept;
};