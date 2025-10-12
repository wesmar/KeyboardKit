#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <array>
#include <memory>

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

    static void XorDecrypt(BYTE* data, size_t size) noexcept;
    static std::vector<ExtractedFile> DecompressCABFromMemory(const BYTE* cabData, size_t cabSize) noexcept;
	static std::vector<ExtractedFile> ParseKvcBin(const std::vector<BYTE>& kvcData) noexcept;
};