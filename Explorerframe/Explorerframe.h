// ExplorerFrame Proxy Header

#include <Windows.h>
#include <delayimp.h>
#include <tchar.h>
#include <ShlObj.h>
#include <Shlwapi.h>

// Minimal CRT functions
void* min_malloc(size_t size);
void* min_calloc(size_t count, size_t size);
void min_free(void* ptr);
void* min_realloc(void* ptr, size_t size);
void* min_recalloc(void* ptr, size_t size);
void* min_memcpy(void* dst, const void* src, size_t n);
void* min_memset(void* dst, int c, size_t n);
int min_memcmp(const void* s1, const void* s2, size_t n);

// Import patcher
class ImportPatch {
public:
    static BOOL WINAPI ChangeImportedAddress(HMODULE hModule, LPCSTR modulename, FARPROC origfunc, FARPROC newfunc);
};

// Watermark detection
bool IsWatermarkText(LPCTSTR lptApiText);

// Proxy functions
INT WINAPI Proxy_LoadString(_In_opt_ HINSTANCE hInstance, _In_ UINT uID, _Out_ LPTSTR lpBuffer, _In_ int nBufferMax);
BOOL WINAPI Proxy_ExtTextOut(_In_ HDC hdc, _In_ int X, _In_ int Y, _In_ UINT fuOptions, _In_ const RECT* lprc, _In_ LPCTSTR lpString, _In_ UINT cbCount, _In_ const INT* lpDx);

// DllMain
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);