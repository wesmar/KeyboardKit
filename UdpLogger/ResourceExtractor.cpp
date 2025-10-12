// ResourceExtractor.cpp
#include "ResourceExtractor.h"
#include "DebugConfig.h"
#include <fdi.h>
#include <SetupAPI.h>
#include <iostream>
#include <map>

#pragma comment(lib, "cabinet.lib")

// =============================================================================
// FDI Callbacks for memory-based CAB extraction
// =============================================================================

struct MemoryReadContext {
    const BYTE* data;
    size_t size;
    size_t offset;
};

// Global context for CAB extraction
static MemoryReadContext* g_cabContext = nullptr;
static ResourceExtractor::ExtractedFile* g_currentFile = nullptr;

// FDI memory allocation callback
static void* DIAMONDAPI fdi_alloc(ULONG cb) {
    return malloc(cb);
}

// FDI memory deallocation callback
static void DIAMONDAPI fdi_free(void* pv) {
    free(pv);
}

// FDI file open callback - returns pointer to memory context
static INT_PTR DIAMONDAPI fdi_open(char* pszFile, int oflag, int pmode) {
    DEBUG_LOG_VERBOSE(L"[FDI] fdi_open called with: " << pszFile);
    
    if (g_cabContext) {
        DEBUG_LOG_VERBOSE(L"[FDI] Returning CAB context");
        return (INT_PTR)g_cabContext;
    }
    
    DEBUG_LOG_VERBOSE(L"[FDI] No CAB context available!");
    return -1;
}

// FDI file read callback - reads from memory buffer
static UINT DIAMONDAPI fdi_read(INT_PTR hf, void* pv, UINT cb) {
    MemoryReadContext* ctx = (MemoryReadContext*)hf;
    if (!ctx) {
        DEBUG_LOG_VERBOSE(L"[FDI] fdi_read: NULL context!");
        return 0;
    }
    
    size_t remaining = ctx->size - ctx->offset;
    size_t to_read = (cb < remaining) ? cb : remaining;
    
    if (to_read > 0) {
        memcpy(pv, ctx->data + ctx->offset, to_read);
        ctx->offset += to_read;
    }
    
    // Debug first read
    static bool firstRead = true;
    if (firstRead && to_read > 0) {
        DEBUG_LOG_VERBOSE(L"[FDI] First read: " << to_read << L" bytes");
        firstRead = false;
    }
    
    return static_cast<UINT>(to_read);
}

// FDI file write callback - writes to current file buffer
static UINT DIAMONDAPI fdi_write(INT_PTR hf, void* pv, UINT cb) {
    if (g_currentFile && cb > 0) {
        BYTE* data = static_cast<BYTE*>(pv);
        g_currentFile->data.insert(g_currentFile->data.end(), data, data + cb);
        
        // Debug output every 10KB
        static size_t totalWritten = 0;
        totalWritten += cb;
        if (totalWritten % 10000 == 0) {
            DEBUG_LOG_VERBOSE(L"[FDI] Written " << totalWritten << L" bytes so far...");
        }
    }
    return cb;
}

// FDI file close callback
static int DIAMONDAPI fdi_close(INT_PTR hf) {
    DEBUG_LOG_VERBOSE(L"[FDI] fdi_close called");
    g_currentFile = nullptr;
    return 0;
}

// FDI file seek callback - seeks in memory buffer
static LONG DIAMONDAPI fdi_seek(INT_PTR hf, LONG dist, int seektype) {
    MemoryReadContext* ctx = (MemoryReadContext*)hf;
    if (!ctx) return -1;
    
    switch (seektype) {
        case SEEK_SET: 
            ctx->offset = dist; 
            break;
        case SEEK_CUR: 
            ctx->offset += dist; 
            break;
        case SEEK_END: 
            ctx->offset = ctx->size + dist; 
            break;
    }
    
    return static_cast<LONG>(ctx->offset);
}

// FDI notification callback - handles file extraction events
static INT_PTR DIAMONDAPI fdi_notify(FDINOTIFICATIONTYPE fdint, PFDINOTIFICATION pfdin) {
    std::vector<ResourceExtractor::ExtractedFile>* files = 
        static_cast<std::vector<ResourceExtractor::ExtractedFile>*>(pfdin->pv);
    
    switch (fdint) {
        case fdintCOPY_FILE: {
            DEBUG_LOG_VERBOSE(L"[FDI] fdintCOPY_FILE triggered");
            
            std::wstring filename;
            int len = MultiByteToWideChar(CP_ACP, 0, pfdin->psz1, -1, nullptr, 0);
            if (len > 0) {
                filename.resize(len);
                MultiByteToWideChar(CP_ACP, 0, pfdin->psz1, -1, &filename[0], len);
            }
            
            DEBUG_LOG(L"[FDI] Found file in CAB: " << filename 
                       << L" (" << pfdin->cb << L" bytes)");
            
            // Extract ALL files from CAB
            ResourceExtractor::ExtractedFile file;
            file.filename = filename;
            file.isValidPE = false;
            files->push_back(file);
            
            g_currentFile = &files->back();
            return (INT_PTR)g_cabContext;
        }
        
        case fdintCLOSE_FILE_INFO:
            DEBUG_LOG_VERBOSE(L"[FDI] fdintCLOSE_FILE_INFO");
            if (g_currentFile) {
                DEBUG_LOG(L"[FDI] Extracted: " << g_currentFile->filename 
                           << L" (" << g_currentFile->data.size() << L" bytes)");
            }
            g_currentFile = nullptr;
            return TRUE;
            
        default:
            break;
    }
    return 0;
}

// =============================================================================
// XOR Decryption
// =============================================================================

void ResourceExtractor::XorDecrypt(BYTE* data, size_t size) noexcept {
    for (size_t i = 0; i < size; ++i) {
        data[i] ^= XOR_KEY[i % XOR_KEY.size()];
    }
}

// =============================================================================
// CAB Decompression from memory
// =============================================================================

std::vector<ResourceExtractor::ExtractedFile> 
ResourceExtractor::DecompressCABFromMemory(const BYTE* cabData, size_t cabSize) noexcept {
    std::vector<ExtractedFile> files;
    
    DEBUG_LOG(L"[CAB] Starting decompression, size: " << cabSize << L" bytes");
    
    if (cabSize >= 4) {
        DEBUG_LOG_VERBOSE(L"[CAB] First 4 bytes: " 
                   << std::hex << (int)cabData[0] << L" " 
                   << (int)cabData[1] << L" " 
                   << (int)cabData[2] << L" " 
                   << (int)cabData[3] << std::dec);
    }
    
    // Setup global context
    MemoryReadContext ctx = { cabData, cabSize, 0 };
    g_cabContext = &ctx;
    
    ERF erf{};
    
    HFDI hfdi = FDICreate(
        fdi_alloc,
        fdi_free,
        fdi_open,
        fdi_read,
        fdi_write,
        fdi_close,
        fdi_seek,
        cpuUNKNOWN,
        &erf
    );
    
    if (!hfdi) {
        DEBUG_LOG(L"[CAB] FDICreate failed!");
        DEBUG_LOG(L"[CAB] Error code: " << erf.erfOper);
        g_cabContext = nullptr;
        return files;
    }
    
    DEBUG_LOG_VERBOSE(L"[CAB] FDICreate succeeded");
    
    // Provide CAB filename (can be arbitrary for memory operations)
    char cabName[] = "memory.cab";
    char cabPath[] = "";
    
    DEBUG_LOG_VERBOSE(L"[CAB] Calling FDICopy...");
    
    BOOL result = FDICopy(hfdi, cabName, cabPath, 0, fdi_notify, nullptr, &files);
    
    if (!result) {
        DEBUG_LOG(L"[CAB] FDICopy failed!");
        DEBUG_LOG(L"[CAB] Error code: " << erf.erfOper << L" Type: " << erf.erfType);
    } else {
        DEBUG_LOG(L"[CAB] FDICopy succeeded, extracted " << files.size() << L" files");
    }
    
    FDIDestroy(hfdi);
    g_cabContext = nullptr;
    
    return files;
}

// =============================================================================
// Parse kvc.bin - Split concatenated PE files
// =============================================================================

std::vector<ResourceExtractor::ExtractedFile> 
ResourceExtractor::ParseKvcBin(const std::vector<BYTE>& kvcData) noexcept {
    std::vector<ExtractedFile> files;
    
    DEBUG_LOG(L"[KVC] Parsing kvc.bin (" << kvcData.size() << L" bytes)");
    
    if (kvcData.size() < 2) {
        DEBUG_LOG(L"[KVC] File too small");
        return files;
    }
    
    // Find all PE signatures (MZ = 0x4D 0x5A)
    std::vector<size_t> peOffsets;
    
    for (size_t i = 0; i < kvcData.size() - 1; i++) {
        if (kvcData[i] == 0x4D && kvcData[i + 1] == 0x5A) {
            peOffsets.push_back(i);
        }
    }
    
    if (peOffsets.size() != 2) {
        DEBUG_LOG(L"[KVC] Expected 2 PE files, found " << peOffsets.size());
        return files;
    }
    
    DEBUG_LOG(L"[KVC] Found 2 PE files");
    
    // Extract both files
    for (size_t i = 0; i < 2; i++) {
        size_t startOffset = peOffsets[i];
        size_t endOffset = (i == 0) ? peOffsets[1] : kvcData.size();
        size_t fileSize = endOffset - startOffset;
        
        ExtractedFile file;
        file.data.assign(kvcData.begin() + startOffset, kvcData.begin() + endOffset);
        file.isValidPE = true;
        
        // Identify file type by checking PE Subsystem in Optional Header
        bool isDriver = false;
        
        if (file.data.size() >= 0x200) {  // Minimum PE size
            // Get PE header offset (stored at 0x3C in DOS header)
            DWORD peOffset = *reinterpret_cast<const DWORD*>(&file.data[0x3C]);
            
            if (peOffset < file.data.size() - 0x5C) {
                // Read Subsystem field (offset +0x5C from PE signature)
                WORD subsystem = *reinterpret_cast<const WORD*>(&file.data[peOffset + 0x5C]);
                
                // IMAGE_SUBSYSTEM_NATIVE = 1 (kernel driver)
                // IMAGE_SUBSYSTEM_WINDOWS_GUI = 2 (DLL)
                isDriver = (subsystem == 1);
                
                DEBUG_LOG_VERBOSE(L"[KVC] File #" << i << L" subsystem: " << subsystem 
                           << (isDriver ? L" (DRIVER)" : L" (DLL)"));
            }
        }
        
        // Assign filename based on subsystem
        if (isDriver) {
            file.filename = L"kvckbd.sys";
        } else {
            file.filename = L"ExpIorerFrame.dll";
        }
        
        DEBUG_LOG(L"[KVC] Identified as " << file.filename 
                   << L" (size: " << fileSize << L" bytes)");
        
        files.push_back(file);
    }
    
    return files;
}

// =============================================================================
// Main extraction function - Complete pipeline
// =============================================================================

std::vector<ResourceExtractor::ExtractedFile> 
ResourceExtractor::ExtractFilesFromResource(HINSTANCE hInstance, int resourceId) noexcept {
    DEBUG_LOG(L"[RES] Extracting files from resource ID: " << resourceId);
    
    // Step 1: Load resource from executable
    HRSRC hRes = FindResourceW(hInstance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) {
        DEBUG_LOG(L"[RES] FindResource failed!");
        return {};
    }
    
    HGLOBAL hResData = LoadResource(hInstance, hRes);
    if (!hResData) {
        DEBUG_LOG(L"[RES] LoadResource failed!");
        return {};
    }
    
    DWORD resSize = SizeofResource(hInstance, hRes);
    const BYTE* resData = static_cast<const BYTE*>(LockResource(hResData));
    if (!resData) {
        DEBUG_LOG(L"[RES] LockResource failed!");
        return {};
    }
    
    DEBUG_LOG(L"[RES] Resource size: " << resSize << L" bytes");
    
    // Step 2: Skip icon data (first 1662 bytes)
    const size_t ICON_SIZE = 1662;
    if (resSize <= ICON_SIZE) {
        DEBUG_LOG(L"[RES] Resource too small!");
        return {};
    }
    
    DEBUG_LOG_VERBOSE(L"[RES] Skipping icon (" << ICON_SIZE << L" bytes)");
    
    // Step 3: Extract encrypted CAB
    size_t cabSize = resSize - ICON_SIZE;
    DEBUG_LOG(L"[RES] Encrypted CAB size: " << cabSize << L" bytes");
    
    std::vector<BYTE> encryptedCab(resData + ICON_SIZE, resData + resSize);
    
    // Step 4: Decrypt CAB using XOR
    XorDecrypt(encryptedCab.data(), encryptedCab.size());
    
    DEBUG_LOG_VERBOSE(L"[RES] First 4 decrypted bytes: ");
    for (int i = 0; i < 4 && i < encryptedCab.size(); i++) {
        DEBUG_LOG_VERBOSE(std::hex << encryptedCab[i] << L" ");
    }
    DEBUG_LOG_VERBOSE(L"[RES] Expected CAB signature: 4D 53 43 46 (MSCF)");
    
    // Step 5: Decompress CAB → get kvc.bin
    auto cabFiles = DecompressCABFromMemory(encryptedCab.data(), encryptedCab.size());
    
    if (cabFiles.empty()) {
        DEBUG_LOG(L"[RES] No files extracted from CAB!");
        return {};
    }
    
    DEBUG_LOG(L"[RES] Extracted " << cabFiles.size() << L" file(s) from CAB");
    
    // Step 6: Find kvc.bin and parse it to separate PE files
    for (auto& cabFile : cabFiles) {
        if (cabFile.filename.find(L"kvc.bin") != std::wstring::npos) {
            DEBUG_LOG(L"[RES] Found kvc.bin, parsing...");
            return ParseKvcBin(cabFile.data);
        }
    }
    
    DEBUG_LOG(L"[RES] kvc.bin not found in CAB!");
    return {};
}