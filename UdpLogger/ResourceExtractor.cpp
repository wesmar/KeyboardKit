#include "ResourceExtractor.h"
#include <cstring>

size_t ResourceExtractor::GetIcoSize(const BYTE* data, size_t dataSize) noexcept {
    if (dataSize < 6) return dataSize;
    
    if (data[0] != 0 || data[1] != 0) return dataSize;
    
    WORD numImages = *reinterpret_cast<const WORD*>(data + 4);
    size_t headerSize = 6 + (numImages * 16);
    
    size_t maxEnd = headerSize;
    
    for (WORD i = 0; i < numImages; ++i) {
        size_t entryOffset = 6 + (i * 16);
        if (entryOffset + 16 > dataSize) break;
        
        DWORD imgSize = *reinterpret_cast<const DWORD*>(data + entryOffset + 8);
        DWORD imgOffset = *reinterpret_cast<const DWORD*>(data + entryOffset + 12);
        
        size_t imgEnd = imgOffset + imgSize;
        if (imgEnd > maxEnd) maxEnd = imgEnd;
    }
    
    return (maxEnd <= dataSize) ? maxEnd : dataSize;
}

void ResourceExtractor::XorDecrypt(BYTE* data, size_t size) noexcept {
    for (size_t i = 0; i < size; ++i) {
        data[i] ^= XOR_KEY[i % XOR_KEY.size()];
    }
}

std::vector<ResourceExtractor::ExtractedFile> 
ResourceExtractor::ParseTLVPayload(const BYTE* payload, size_t size) noexcept {
    std::vector<ExtractedFile> files;
    
    if (size < 4) return files;
    
    DWORD numFiles = *reinterpret_cast<const DWORD*>(payload);
    size_t offset = 4;
    
    for (DWORD i = 0; i < numFiles && offset < size; ++i) {
        // Odczytaj długość nazwy
        if (offset + 4 > size) break;
        
        DWORD nameLen = *reinterpret_cast<const DWORD*>(payload + offset);
        offset += 4;
        
        // Odczytaj nazwę
        if (offset + nameLen > size) break;
        
        std::string nameUtf8(reinterpret_cast<const char*>(payload + offset), nameLen);
        std::wstring filename(nameUtf8.begin(), nameUtf8.end());
        offset += nameLen;
        
        // ✓ POPRAWKA: Odczytaj długość danych
        if (offset + 4 > size) break;
        
        DWORD dataLen = *reinterpret_cast<const DWORD*>(payload + offset);
        offset += 4;
        
        // Odczytaj dane
        if (offset + dataLen > size) break;
        
        const BYTE* fileData = payload + offset;
        
        ExtractedFile file;
        file.filename = filename;
        file.data.assign(fileData, fileData + dataLen);
        file.isValidPE = true;  // zakładamy że dane są poprawne
        
        files.push_back(std::move(file));
        offset += dataLen;
    }
    
    return files;
}

std::vector<ResourceExtractor::ExtractedFile> 
ResourceExtractor::ExtractFilesFromResource(HINSTANCE hInstance, int resourceId) noexcept {
    HRSRC hRes = FindResourceW(hInstance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return {};
    
    HGLOBAL hResData = LoadResource(hInstance, hRes);
    if (!hResData) return {};
    
    DWORD resSize = SizeofResource(hInstance, hRes);
    const BYTE* resData = static_cast<const BYTE*>(LockResource(hResData));
    if (!resData) return {};
    
    size_t icoSize = GetIcoSize(resData, resSize);
    if (icoSize >= resSize) return {};
    
    size_t payloadSize = resSize - icoSize;
    std::vector<BYTE> payload(resData + icoSize, resData + resSize);
    
    XorDecrypt(payload.data(), payload.size());
    
    return ParseTLVPayload(payload.data(), payload.size());
}