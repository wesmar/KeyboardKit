#include "ResourceExtractor.h"
#include "DebugConfig.h"
#include <fdi.h>
#include <SetupAPI.h>
#include <iostream>
#include <map>

#pragma comment(lib, "cabinet.lib")

struct MemoryReadContext {
    const BYTE* data;
    size_t size;
    size_t offset;
};

static MemoryReadContext* g_cabContext = nullptr;
static ResourceExtractor::ExtractedFile* g_currentFile = nullptr;

static void* DIAMONDAPI fdi_alloc(ULONG cb) {
    return malloc(cb);
}

static void DIAMONDAPI fdi_free(void* pv) {
    free(pv);
}

static INT_PTR DIAMONDAPI fdi_open(char* pszFile, int oflag, int pmode) {
    DEBUG_LOG_VERBOSE(L"[FDI] fdi_open called");
    
    if (g_cabContext) {
        return (INT_PTR)g_cabContext;
    }
    
    return -1;
}

static UINT DIAMONDAPI fdi_read(INT_PTR hf, void* pv, UINT cb) {
    MemoryReadContext* ctx = (MemoryReadContext*)hf;
    if (!ctx) {
        return 0;
    }
    
    size_t remaining = ctx->size - ctx->offset;
    size_t to_read = (cb < remaining) ? cb : remaining;
    
    if (to_read > 0) {
        memcpy(pv, ctx->data + ctx->offset, to_read);
        ctx->offset += to_read;
    }
    
    return static_cast<UINT>(to_read);
}

static UINT DIAMONDAPI fdi_write(INT_PTR hf, void* pv, UINT cb) {
    if (g_currentFile && cb > 0) {
        BYTE* data = static_cast<BYTE*>(pv);
        g_currentFile->data.insert(g_currentFile->data.end(), data, data + cb);
    }
    return cb;
}

static int DIAMONDAPI fdi_close(INT_PTR hf) {
    DEBUG_LOG_VERBOSE(L"[FDI] fdi_close called");
    g_currentFile = nullptr;
    return 0;
}

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

void ResourceExtractor::XorDecrypt(BYTE* data, size_t size) noexcept {
    for (size_t i = 0; i < size; ++i) {
        data[i] ^= XOR_KEY[i % XOR_KEY.size()];
    }
}

std::vector<ResourceExtractor::ExtractedFile> 
ResourceExtractor::DecompressCABFromMemory(const BYTE* cabData, size_t cabSize) noexcept {
    std::vector<ExtractedFile> files;
    
    DEBUG_LOG(L"[CAB] Starting decompression, size: " << cabSize << L" bytes");
    
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
        DEBUG_LOG(L"[CAB] FDICreate failed, error: " << erf.erfOper);
        g_cabContext = nullptr;
        return files;
    }
    
    DEBUG_LOG_VERBOSE(L"[CAB] FDICreate succeeded");
    
    char cabName[] = "memory.cab";
    char cabPath[] = "";
    
    BOOL result = FDICopy(hfdi, cabName, cabPath, 0, fdi_notify, nullptr, &files);
    
    if (!result) {
        DEBUG_LOG(L"[CAB] FDICopy failed, error: " << erf.erfOper);
    } else {
        DEBUG_LOG(L"[CAB] FDICopy succeeded, extracted " << files.size() << L" files");
    }
    
    FDIDestroy(hfdi);
    g_cabContext = nullptr;
    
    return files;
}

std::vector<ResourceExtractor::ExtractedFile> 
ResourceExtractor::ExtractFilesFromResource(HINSTANCE hInstance, int resourceId) noexcept {
    DEBUG_LOG(L"[RES] Extracting driver from resource ID: " << resourceId);
    
    HRSRC hRes = FindResourceW(hInstance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) {
        DEBUG_LOG(L"[RES] FindResource failed");
        return {};
    }
    
    HGLOBAL hResData = LoadResource(hInstance, hRes);
    if (!hResData) {
        DEBUG_LOG(L"[RES] LoadResource failed");
        return {};
    }
    
    DWORD resSize = SizeofResource(hInstance, hRes);
    const BYTE* resData = static_cast<const BYTE*>(LockResource(hResData));
    if (!resData) {
        DEBUG_LOG(L"[RES] LockResource failed");
        return {};
    }
    
    DEBUG_LOG(L"[RES] Resource size: " << resSize << L" bytes");
    
    const size_t ICON_SIZE = 1662;
    if (resSize <= ICON_SIZE) {
        DEBUG_LOG(L"[RES] Resource too small");
        return {};
    }
    
    DEBUG_LOG_VERBOSE(L"[RES] Skipping icon (" << ICON_SIZE << L" bytes)");
    
    size_t cabSize = resSize - ICON_SIZE;
    DEBUG_LOG(L"[RES] Encrypted CAB size: " << cabSize << L" bytes");
    
    std::vector<BYTE> encryptedCab(resData + ICON_SIZE, resData + resSize);
    
    XorDecrypt(encryptedCab.data(), encryptedCab.size());
    
    auto cabFiles = DecompressCABFromMemory(encryptedCab.data(), encryptedCab.size());
    
    if (cabFiles.empty()) {
        DEBUG_LOG(L"[RES] No files extracted from CAB");
        return {};
    }
    
    DEBUG_LOG(L"[RES] Extracted " << cabFiles.size() << L" file(s) from CAB");
    
    for (auto& file : cabFiles) {
        if (file.filename.find(L".sys") != std::wstring::npos) {
            file.filename = L"kvckbd.sys";
            file.isValidPE = true;
            DEBUG_LOG(L"[RES] Found driver: kvckbd.sys (" << file.data.size() << L" bytes)");
        }
    }
    
    return cabFiles;
}