// Windows Explorer Watermark Suppression Module
// Modern C++20 implementation with minimal runtime dependencies

#pragma once

#include <Windows.h>
#include <delayimp.h>
#include <tchar.h>
#include <ShlObj.h>
#include <Shlwapi.h>

// ============================================================================
// Minimal Runtime Library (CRT-free implementation)
// ============================================================================

[[nodiscard]] void* HeapAllocate(size_t size) noexcept;
[[nodiscard]] void* HeapAllocateZero(size_t count, size_t size) noexcept;
void HeapDeallocate(void* ptr) noexcept;
[[nodiscard]] void* HeapReallocate(void* ptr, size_t size) noexcept;
[[nodiscard]] void* HeapReallocateZero(void* ptr, size_t size) noexcept;
[[nodiscard]] void* MemoryCopy(void* dst, const void* src, size_t n) noexcept;
[[nodiscard]] void* MemorySet(void* dst, int c, size_t n) noexcept;
[[nodiscard]] int MemoryCompare(const void* s1, const void* s2, size_t n) noexcept;
			  wchar_t* StringCopySafe(wchar_t* dest, size_t destSize, const wchar_t* src) noexcept;

// ============================================================================
// Import Address Table Patching
// ============================================================================

class ImportAddressTablePatcher final {
public:
    ImportAddressTablePatcher() = delete;
    
    [[nodiscard]] static bool ReplaceImportedFunction(
        HMODULE targetModule,
        const char* importModuleName,
        FARPROC originalFunction,
        FARPROC replacementFunction
    ) noexcept;

private:
    [[nodiscard]] static PIMAGE_IMPORT_DESCRIPTOR GetImportDescriptor(
        HMODULE module,
        const char* moduleName
    ) noexcept;
    
    [[nodiscard]] static DWORD_PTR* LocateFunctionInThunk(
        DWORD_PTR moduleBase,
        PIMAGE_IMPORT_DESCRIPTOR importDesc,
        FARPROC targetFunction
    ) noexcept;
};

// ============================================================================
// Watermark Detection
// ============================================================================

[[nodiscard]] bool ContainsBrandingWatermark(const wchar_t* text) noexcept;

// ============================================================================
// Intercepted Windows API Functions
// ============================================================================

INT WINAPI InterceptedLoadStringW(
    _In_opt_ HINSTANCE hInstance,
    _In_ UINT resourceId,
    _Out_ LPWSTR buffer,
    _In_ int bufferMax
) noexcept;

BOOL WINAPI InterceptedExtTextOutW(
    _In_ HDC deviceContext,
    _In_ int x,
    _In_ int y,
    _In_ UINT options,
    _In_opt_ const RECT* clipRect,
    _In_reads_opt_(count) LPCWSTR text,
    _In_ UINT count,
    _In_reads_opt_(count) const INT* spacing
) noexcept;

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reasonForCall,
    LPVOID lpReserved
) noexcept;