// ExplorerFrame Proxy DLL
// Watermark disabler for Windows Explorer

#include "Explorerframe.h"

// Redirect standard functions to minimal CRT
#define malloc min_malloc
#define calloc min_calloc
#define free min_free
#define realloc min_realloc
#define memcpy min_memcpy
#define memset min_memset
#define memcmp min_memcmp

// Export original ExplorerFrame functions
#pragma comment(linker, "/export:DllGetClassObject=explorerframe.DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllCanUnloadNow=explorerframe.DllCanUnloadNow,PRIVATE")

#define Length(a) (sizeof(a)/sizeof(a[0]))
static LPTSTR lpchNames[7];

// String search function
int indexOf(LPCTSTR source, int sourceOffset, int sourceCount,
    LPCTSTR target, int targetOffset, int targetCount, int fromIndex) {
    if (fromIndex >= sourceCount) return (targetCount == 0 ? sourceCount : -1);
    if (fromIndex < 0) fromIndex = 0;
    if (targetCount == 0) return fromIndex;

    TCHAR first = target[targetOffset];
    int max = sourceOffset + (sourceCount - targetCount);

    for (int i = sourceOffset + fromIndex; i <= max; i++) {
        if (source[i] != first) {
            while (++i <= max && source[i] != first);
        }
        if (i <= max) {
            int j = i + 1;
            int end = j + targetCount - 1;
            for (int k = targetOffset + 1; j < end && source[j] == target[k]; j++, k++);
            if (j == end) return i - sourceOffset;
        }
    }
    return -1;
}

// Watermark text detection
bool IsWatermarkText(LPCTSTR lptApiText) {
    const TCHAR lptWs = '%';
    if (!lptApiText || lptApiText[0] == 0) return false;

    int lenApiText = static_cast<int>(_tcslen(lptApiText));
    for (int i = 0; i < Length(lpchNames); i++) {
        if (lpchNames[i] && lpchNames[i][0] != 0) {
            int stPoint = 0;
            int lenWaterText = static_cast<int>(_tcslen(lpchNames[i]));
            if (i == 3 && lpchNames[i][0] == lptWs && lenWaterText > 4) {
                stPoint = 4;
                while ((++stPoint < lenWaterText) && (lpchNames[i][stPoint] != lptWs));
                lenWaterText = stPoint - 4;
                stPoint = 4;
            }
            if (indexOf(lptApiText, 0, lenApiText, lpchNames[i], stPoint, lenWaterText, 0) >= 0)
                return true;
        }
    }
    return false;
}

// LoadString proxy
INT WINAPI Proxy_LoadString(_In_opt_ HINSTANCE hInstance, _In_ UINT uID,
    _Out_ LPTSTR lpBuffer, _In_ int nBufferMax) {
    if (uID == 62000 || uID == 62001) return 0;
    return LoadString(hInstance, uID, lpBuffer, nBufferMax);
}

// ExtTextOut proxy
BOOL WINAPI Proxy_ExtTextOut(_In_ HDC hdc, _In_ int X, _In_ int Y, _In_ UINT fuOptions,
    _In_ const RECT* lprc, _In_ LPCTSTR lpString, _In_ UINT cbCount, _In_ const INT* lpDx) {
    if (IsWatermarkText(lpString)) return TRUE;
    return ExtTextOutW(hdc, X, Y, fuOptions, lprc, lpString, cbCount, lpDx);
}

// Import address patcher
BOOL WINAPI ImportPatch::ChangeImportedAddress(HMODULE hModule, LPCSTR modulename,
    FARPROC origfunc, FARPROC newfunc) {
    DWORD_PTR lpFileBase = (DWORD_PTR)hModule;
    if (!lpFileBase || !modulename || !origfunc || !newfunc) return FALSE;

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)lpFileBase;
    PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS)(lpFileBase + dosHeader->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR pImportDir = (PIMAGE_IMPORT_DESCRIPTOR)
        (lpFileBase + pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    while (pImportDir->Name) {
        LPSTR name = (LPSTR)(lpFileBase + pImportDir->Name);
        if (lstrcmpiA(name, modulename) == 0) break;
        pImportDir++;
    }
    if (!pImportDir->Name) return FALSE;

    DWORD_PTR* pFunctions = (DWORD_PTR*)(lpFileBase + pImportDir->FirstThunk);
    while (*pFunctions) {
        if (*pFunctions == (DWORD_PTR)origfunc) break;
        pFunctions++;
    }
    if (!*pFunctions) return FALSE;

    DWORD oldpr;
    VirtualProtect(pFunctions, sizeof(DWORD_PTR), PAGE_EXECUTE_READWRITE, &oldpr);
    *pFunctions = (DWORD_PTR)newfunc;
    VirtualProtect(pFunctions, sizeof(DWORD_PTR), oldpr, &oldpr);
    return TRUE;
}

// Minimal CRT implementation
void* min_malloc(size_t size) { return HeapAlloc(GetProcessHeap(), 0, size); }
void* min_calloc(size_t count, size_t size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * size); }
void min_free(void* ptr) { if (ptr) HeapFree(GetProcessHeap(), 0, ptr); }
void* min_realloc(void* ptr, size_t size) {
    return ptr ? HeapReAlloc(GetProcessHeap(), 0, ptr, size) : HeapAlloc(GetProcessHeap(), 0, size);
}
void* min_recalloc(void* ptr, size_t size) {
    return ptr ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ptr, size) : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}
void* min_memcpy(void* dst, const void* src, size_t n) {
    char* q = (char*)dst; const char* p = (const char*)src;
    while (n--) *q++ = *p++; return dst;
}
void* min_memset(void* dst, int c, size_t n) {
    char* q = (char*)dst; while (n--) *q++ = c; return dst;
}
int min_memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* c1 = (const unsigned char*)s1, * c2 = (const unsigned char*)s2;
    int d = 0; while (n--) { d = (int)*c1++ - (int)*c2++; if (d) break; } return d;
}

// C++ operators
void* operator new(size_t size) { return min_malloc(size); }
void operator delete(void* ptr) { min_free(ptr); }
void* operator new[](size_t size) { return min_malloc(size); }
void operator delete[](void* ptr) { min_free(ptr); }

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        HMODULE g_hShell32 = GetModuleHandle(_T("shell32.dll"));
        if (g_hShell32) {
            BOOL bImportChanged = FALSE;
            
            // Patch LoadString
            FARPROC pLoadString = GetProcAddress(GetModuleHandle(_T("api-ms-win-core-libraryloader-l1-2-0.dll")), "LoadStringW");
            if (!pLoadString) pLoadString = GetProcAddress(GetModuleHandle(_T("api-ms-win-core-libraryloader-l1-1-1.dll")), "LoadStringW");
            if (pLoadString) bImportChanged = ImportPatch::ChangeImportedAddress(g_hShell32, 
                pLoadString == GetProcAddress(GetModuleHandle(_T("api-ms-win-core-libraryloader-l1-1-1.dll")), "LoadStringW") ? 
                "api-ms-win-core-libraryloader-l1-1-1.dll" : "api-ms-win-core-libraryloader-l1-2-0.dll", pLoadString, (FARPROC)Proxy_LoadString);

            // Patch ExtTextOut
            FARPROC pExtTextOut = GetProcAddress(GetModuleHandle(_T("gdi32.dll")), "ExtTextOutW");
            if (pExtTextOut) bImportChanged = ImportPatch::ChangeImportedAddress(g_hShell32, "gdi32.dll", pExtTextOut, (FARPROC)Proxy_ExtTextOut);

            if (bImportChanged) {
                // Initialize watermark strings
                LPCTSTR lptBrand = _T("Windows ");
                LPCTSTR lptPr = _T("Build ");
                int iLpNamesSize[] = { 128, 64, 64, 128, 128, 167, 128 };

                for (BYTE i = 0; i < Length(lpchNames); i++)
                    lpchNames[i] = (LPTSTR)min_malloc(iLpNamesSize[i] * sizeof(TCHAR));

                // Get Windows branding
                HMODULE hBrand = LoadLibrary(_T("winbrand.dll"));
                if (hBrand) {
                    typedef BOOL(WINAPI* BrandLoadStr_t)(LPCTSTR, INT, LPTSTR, INT);
                    BrandLoadStr_t BrandLoadStr = (BrandLoadStr_t)GetProcAddress(hBrand, "BrandingLoadString");
                    if (BrandLoadStr && !BrandLoadStr(_T("Basebrd"), 12, lpchNames[0], iLpNamesSize[0]))
                        _tcscpy(lpchNames[0], lptBrand);
                    FreeLibrary(hBrand);
                }
                else _tcscpy(lpchNames[0], lptBrand);

                // Get watermark strings from shell32
                HMODULE hShell = GetModuleHandle(_T("shell32.dll"));
                if (hShell) {
                    UINT uiID[] = { 33088, 33089, 33108, 33109, 33111, 33117 };
                    for (int i = 0; i < Length(uiID); i++)
                        if (!LoadString(hShell, uiID[i], lpchNames[i + 1], iLpNamesSize[i + 1]))
                            _tcscpy(lpchNames[i + 1], lptPr);
                }
                else for (BYTE i = 1; i < Length(lpchNames); i++)
                    _tcscpy(lpchNames[i], lptPr);
            }
        }
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}