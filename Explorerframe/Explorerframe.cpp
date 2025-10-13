// Windows Explorer Watermark Suppression Module
// Modern C++20 implementation targeting x64 Windows 11

#include "Explorerframe.h"

// Redirect standard functions to minimal runtime
#define malloc HeapAllocate
#define calloc HeapAllocateZero
#define free HeapDeallocate
#define realloc HeapReallocate
#define memcpy MemoryCopy
#define memset MemorySet
#define memcmp MemoryCompare

// Forward original ExplorerFrame.dll exports
#pragma comment(linker, "/export:DllGetClassObject=explorerframe.DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllCanUnloadNow=explorerframe.DllCanUnloadNow,PRIVATE")

// ============================================================================
// Constants & Configuration
// ============================================================================

namespace {
    constexpr size_t BRANDING_PATTERN_COUNT = 7;
    constexpr wchar_t WILDCARD_MARKER = L'%';
    
    // Resource IDs for watermark strings stored in shell32.dll
    constexpr UINT SHELL32_WATERMARK_IDS[] = {
        33088, 33089, 33108, 33109, 33111, 33117
    };
    
    // LoadString resource IDs to block
    constexpr UINT BLOCKED_RESOURCE_ID_1 = 62000;
    constexpr UINT BLOCKED_RESOURCE_ID_2 = 62001;
    
    // Buffer sizes for branding pattern storage
    constexpr int PATTERN_BUFFER_SIZES[BRANDING_PATTERN_COUNT] = {
        128, 64, 64, 128, 128, 167, 128
    };
    
    // Global storage for detected watermark patterns
    wchar_t* g_brandingPatterns[BRANDING_PATTERN_COUNT] = {};
}

// ============================================================================
// Minimal Runtime Library Implementation
// ============================================================================

[[nodiscard]] void* HeapAllocate(const size_t size) noexcept {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

[[nodiscard]] void* HeapAllocateZero(const size_t count, const size_t size) noexcept {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * size);
}

void HeapDeallocate(void* const ptr) noexcept {
    if (ptr) {
        HeapFree(GetProcessHeap(), 0, ptr);
    }
}

[[nodiscard]] void* HeapReallocate(void* const ptr, const size_t size) noexcept {
    return ptr 
        ? HeapReAlloc(GetProcessHeap(), 0, ptr, size)
        : HeapAlloc(GetProcessHeap(), 0, size);
}

[[nodiscard]] void* HeapReallocateZero(void* const ptr, const size_t size) noexcept {
    return ptr
        ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ptr, size)
        : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

[[nodiscard]] void* MemoryCopy(void* const dst, const void* const src, size_t n) noexcept {
    auto* dest = static_cast<char*>(dst);
    auto* source = static_cast<const char*>(src);
    while (n--) {
        *dest++ = *source++;
    }
    return dst;
}

[[nodiscard]] void* MemorySet(void* const dst, const int c, size_t n) noexcept {
    auto* dest = static_cast<char*>(dst);
    while (n--) {
        *dest++ = static_cast<char>(c);
    }
    return dst;
}

[[nodiscard]] int MemoryCompare(const void* const s1, const void* const s2, size_t n) noexcept {
    auto* p1 = static_cast<const unsigned char*>(s1);
    auto* p2 = static_cast<const unsigned char*>(s2);
    
    int difference = 0;
    while (n--) {
        difference = static_cast<int>(*p1++) - static_cast<int>(*p2++);
        if (difference) {
            break;
        }
    }
    return difference;
}

wchar_t* StringCopySafe(wchar_t* const dest, const size_t destSize, const wchar_t* const src) noexcept {
    if (!dest || !src || destSize == 0) {
        return dest;
    }
    
    size_t i = 0;
    while (i < destSize - 1 && src[i] != L'\0') {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = L'\0';
    return dest;
}

// ============================================================================
// C++ Memory Operators
// ============================================================================

[[nodiscard]] void* operator new(const size_t size) {
    return HeapAllocate(size);
}

void operator delete(void* const ptr) noexcept {
    HeapDeallocate(ptr);
}

[[nodiscard]] void* operator new[](const size_t size) {
    return HeapAllocate(size);
}

void operator delete[](void* const ptr) noexcept {
    HeapDeallocate(ptr);
}

// ============================================================================
// String Searching Algorithm
// ============================================================================

namespace {
    // Fast substring search implementation
    [[nodiscard]] constexpr int FindSubstringOffset(
        const wchar_t* source,
        const int sourceOffset,
        const int sourceCount,
        const wchar_t* target,
        const int targetOffset,
        const int targetCount,
        const int fromIndex
    ) noexcept {
        if (fromIndex >= sourceCount) {
            return targetCount == 0 ? sourceCount : -1;
        }
        
        const int adjustedFromIndex = fromIndex < 0 ? 0 : fromIndex;
        
        if (targetCount == 0) {
            return adjustedFromIndex;
        }
        
        const wchar_t firstChar = target[targetOffset];
        const int maxSearchIndex = sourceOffset + (sourceCount - targetCount);
        
        for (int i = sourceOffset + adjustedFromIndex; i <= maxSearchIndex; i++) {
            // Skip to first character match
            if (source[i] != firstChar) {
                while (++i <= maxSearchIndex && source[i] != firstChar);
            }
            
            if (i <= maxSearchIndex) {
                int j = i + 1;
                const int end = j + targetCount - 1;
                int k = targetOffset + 1;
                
                // Verify full match
                while (j < end && source[j] == target[k]) {
                    ++j;
                    ++k;
                }
                
                if (j == end) {
                    return i - sourceOffset;
                }
            }
        }
        
        return -1;
    }
}

// ============================================================================
// Watermark Detection
// ============================================================================

[[nodiscard]] bool ContainsBrandingWatermark(const wchar_t* const text) noexcept {
    if (!text || text[0] == L'\0') {
        return false;
    }
    
    const int textLength = static_cast<int>(wcslen(text));
    
    for (size_t i = 0; i < BRANDING_PATTERN_COUNT; ++i) {
        const wchar_t* pattern = g_brandingPatterns[i];
        
        if (!pattern || pattern[0] == L'\0') {
            continue;
        }
        
        int searchStart = 0;
        int patternLength = static_cast<int>(wcslen(pattern));
        
        // Handle wildcard pattern format: %xxx%yyy%
        // Extract middle segment for matching
        if (i == 3 && pattern[0] == WILDCARD_MARKER && patternLength > 4) {
            searchStart = 4;
            
            while (++searchStart < patternLength && pattern[searchStart] != WILDCARD_MARKER);
            
            patternLength = searchStart - 4;
            searchStart = 4;
        }
        
        if (FindSubstringOffset(text, 0, textLength, pattern, searchStart, patternLength, 0) >= 0) {
            return true;
        }
    }
    
    return false;
}

// ============================================================================
// Intercepted Windows API Functions
// ============================================================================

INT WINAPI InterceptedLoadStringW(
    _In_opt_ const HINSTANCE hInstance,
    _In_ const UINT resourceId,
    _Out_ const LPWSTR buffer,
    _In_ const int bufferMax
) noexcept {
    // Block watermark resource strings
    if (resourceId == BLOCKED_RESOURCE_ID_1 || resourceId == BLOCKED_RESOURCE_ID_2) {
        return 0;
    }
    
    return LoadStringW(hInstance, resourceId, buffer, bufferMax);
}

BOOL WINAPI InterceptedExtTextOutW(
    _In_ const HDC deviceContext,
    _In_ const int x,
    _In_ const int y,
    _In_ const UINT options,
    _In_opt_ const RECT* const clipRect,
    _In_reads_opt_(count) const LPCWSTR text,
    _In_ const UINT count,
    _In_reads_opt_(count) const INT* const spacing
) noexcept {
    // Suppress text rendering if it contains watermark
    if (ContainsBrandingWatermark(text)) {
        return TRUE;
    }
    
    return ExtTextOutW(deviceContext, x, y, options, clipRect, text, count, spacing);
}

// ============================================================================
// Import Address Table Patching
// ============================================================================

[[nodiscard]] PIMAGE_IMPORT_DESCRIPTOR ImportAddressTablePatcher::GetImportDescriptor(
    const HMODULE module,
    const char* const moduleName
) noexcept {
    const DWORD_PTR moduleBase = reinterpret_cast<DWORD_PTR>(module);
    
    if (!moduleBase || !moduleName) {
        return nullptr;
    }
    
    const PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    const PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
    
    PIMAGE_IMPORT_DESCRIPTOR importDescriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
        moduleBase + ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress
    );
    
    // Locate target module in import directory
    while (importDescriptor->Name) {
        const LPCSTR name = reinterpret_cast<LPCSTR>(moduleBase + importDescriptor->Name);
        
        if (lstrcmpiA(name, moduleName) == 0) {
            return importDescriptor;
        }
        
        ++importDescriptor;
    }
    
    return nullptr;
}

[[nodiscard]] DWORD_PTR* ImportAddressTablePatcher::LocateFunctionInThunk(
    const DWORD_PTR moduleBase,
    const PIMAGE_IMPORT_DESCRIPTOR importDesc,
    const FARPROC targetFunction
) noexcept {
    DWORD_PTR* functionPointers = reinterpret_cast<DWORD_PTR*>(moduleBase + importDesc->FirstThunk);
    
    // Search for target function address in IAT
    while (*functionPointers) {
        if (*functionPointers == reinterpret_cast<DWORD_PTR>(targetFunction)) {
            return functionPointers;
        }
        ++functionPointers;
    }
    
    return nullptr;
}

[[nodiscard]] bool ImportAddressTablePatcher::ReplaceImportedFunction(
    const HMODULE targetModule,
    const char* const importModuleName,
    const FARPROC originalFunction,
    const FARPROC replacementFunction
) noexcept {
    const DWORD_PTR moduleBase = reinterpret_cast<DWORD_PTR>(targetModule);
    
    if (!moduleBase || !importModuleName || !originalFunction || !replacementFunction) {
        return false;
    }
    
    PIMAGE_IMPORT_DESCRIPTOR importDescriptor = GetImportDescriptor(targetModule, importModuleName);
    if (!importDescriptor) {
        return false;
    }
    
    DWORD_PTR* functionPointer = LocateFunctionInThunk(moduleBase, importDescriptor, originalFunction);
    if (!functionPointer) {
        return false;
    }
    
    // Modify page protection to allow writing
    DWORD oldProtection;
    if (!VirtualProtect(functionPointer, sizeof(DWORD_PTR), PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    
    // Perform IAT patching
    *functionPointer = reinterpret_cast<DWORD_PTR>(replacementFunction);
    
	// Restore original protection
	(void)VirtualProtect(functionPointer, sizeof(DWORD_PTR), oldProtection, &oldProtection);
    
    return true;
}

// ============================================================================
// Initialization
// ============================================================================

namespace {
    void InitializeBrandingPatterns(const HMODULE shell32Module) noexcept {
        constexpr wchar_t DEFAULT_BRANDING[] = L"Windows ";
        constexpr wchar_t DEFAULT_BUILD[] = L"Build ";
        
        // Allocate storage for all patterns
        for (size_t i = 0; i < BRANDING_PATTERN_COUNT; ++i) {
            g_brandingPatterns[i] = static_cast<wchar_t*>(
                HeapAllocate(PATTERN_BUFFER_SIZES[i] * sizeof(wchar_t))
            );
        }
        
        // Load Windows branding string from winbrand.dll
        if (const HMODULE brandingModule = LoadLibraryW(L"winbrand.dll")) {
            using BrandLoadStringFunc = BOOL(WINAPI*)(LPCWSTR, INT, LPWSTR, INT);
            
            const auto BrandLoadString = reinterpret_cast<BrandLoadStringFunc>(
                GetProcAddress(brandingModule, "BrandingLoadString")
            );
            
            if (BrandLoadString) {
                if (!BrandLoadString(L"Basebrd", 12, g_brandingPatterns[0], PATTERN_BUFFER_SIZES[0])) {
                    StringCopySafe(g_brandingPatterns[0], PATTERN_BUFFER_SIZES[0], DEFAULT_BRANDING);
                }
            }
            
            FreeLibrary(brandingModule);
        } else {
            StringCopySafe(g_brandingPatterns[0], PATTERN_BUFFER_SIZES[0], DEFAULT_BRANDING);
        }
        
        // Load watermark strings from shell32.dll resources
        if (shell32Module) {
            for (size_t i = 0; i < sizeof(SHELL32_WATERMARK_IDS) / sizeof(SHELL32_WATERMARK_IDS[0]); ++i) {
                if (!LoadStringW(
                    shell32Module,
                    SHELL32_WATERMARK_IDS[i],
                    g_brandingPatterns[i + 1],
                    PATTERN_BUFFER_SIZES[i + 1]
                )) {
                    StringCopySafe(g_brandingPatterns[i + 1], PATTERN_BUFFER_SIZES[i + 1], DEFAULT_BUILD);
                }
            }
        } else {
            for (size_t i = 1; i < BRANDING_PATTERN_COUNT; ++i) {
                StringCopySafe(g_brandingPatterns[i], PATTERN_BUFFER_SIZES[i], DEFAULT_BUILD);
            }
        }
    }
    
    bool PatchShell32Imports() noexcept {
        const HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
        if (!shell32) {
            return false;
        }
        
        bool successfullyPatched = false;
        
        // Locate and patch LoadStringW
        FARPROC loadStringFunc = GetProcAddress(
            GetModuleHandleW(L"api-ms-win-core-libraryloader-l1-2-0.dll"),
            "LoadStringW"
        );
        
        if (!loadStringFunc) {
            loadStringFunc = GetProcAddress(
                GetModuleHandleW(L"api-ms-win-core-libraryloader-l1-1-1.dll"),
                "LoadStringW"
            );
        }
        
        if (loadStringFunc) {
            const char* loaderModule = (loadStringFunc == GetProcAddress(
                GetModuleHandleW(L"api-ms-win-core-libraryloader-l1-1-1.dll"),
                "LoadStringW"
            )) ? "api-ms-win-core-libraryloader-l1-1-1.dll"
               : "api-ms-win-core-libraryloader-l1-2-0.dll";
            
            successfullyPatched = ImportAddressTablePatcher::ReplaceImportedFunction(
                shell32,
                loaderModule,
                loadStringFunc,
                reinterpret_cast<FARPROC>(InterceptedLoadStringW)
            );
        }
        
        // Locate and patch ExtTextOutW
        if (const FARPROC extTextOutFunc = GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "ExtTextOutW")) {
            successfullyPatched = ImportAddressTablePatcher::ReplaceImportedFunction(
                shell32,
                "gdi32.dll",
                extTextOutFunc,
                reinterpret_cast<FARPROC>(InterceptedExtTextOutW)
            ) || successfullyPatched;
        }
        
        return successfullyPatched;
    }
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(
    const HMODULE hModule,
    const DWORD reasonForCall,
    LPVOID lpReserved
) noexcept {
    if (reasonForCall == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        const HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
        if (shell32) {
            // Always initialize patterns!
            InitializeBrandingPatterns(shell32);
            // And then try patching
            PatchShell32Imports();
        }
    }
    
    return TRUE;
}