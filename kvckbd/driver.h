/*++
Module Name: driver.h
Abstract: Common definitions and structures.
Environment: Kernel mode only.
--*/

#pragma once

#include <ntifs.h>
#include <ntddk.h>
#include <ntddkbd.h>
#include <ntstrsafe.h>

// External References
extern POBJECT_TYPE* IoDriverObjectType;

// --- GLOBALNA FLAGA RESETU SIECI ---
// Używana do komunikacji między scancode.c (DISPATCH_LEVEL) a driver.c (PASSIVE_LEVEL)
extern volatile LONG g_NetworkResetNeeded;

// --- ARCHITECTURE OFFSETS ---
// Definicje na sztywno, aby uniknąć błędów "undeclared identifier"
#ifdef _WIN64
    #define REMOVE_LOCK_OFFSET_DE      0x20
    #define SPIN_LOCK_OFFSET_DE        0xA0
    #define READ_QUEUE_OFFSET_DE       0xA8
#else
    #define REMOVE_LOCK_OFFSET_DE      0x10
    #define SPIN_LOCK_OFFSET_DE        0x6C
    #define READ_QUEUE_OFFSET_DE       0x70
#endif

// Debugowanie
#ifdef DBG
    #define KBDTRACE_ROUTINES               0x00000001
    #define KBDTRACE_OPERATION_STATUS       0x00000002
    const static ULONG gTraceFlags = KBDTRACE_ROUTINES | KBDTRACE_OPERATION_STATUS;
    #define KBD_ERROR(_fmt, ...)    DbgPrint("KBD ERROR: " _fmt "\n", __VA_ARGS__)
    #define KBD_INFO(_fmt, ...)     DbgPrint("KBD INFO: " _fmt "\n", __VA_ARGS__)
#else
    #define KBD_ERROR(_fmt, ...)    ((void)0)
    #define KBD_INFO(_fmt, ...)     ((void)0)
#endif

#define KBDDRIVER_POOL_TAG      'dbKK'

// Domyślna konfiguracja
#define KBDDRIVER_REMOTE_IP         L"127.0.0.1"
#define KBDDRIVER_REMOTE_PORT       L"31415"

// --- STRUKTURY ---

typedef struct _KBDDRIVER_KEYBOARD_OBJECT
{
    LIST_ENTRY          ListEntry;
    BOOLEAN             InitSuccess;
    BOOLEAN             SafeUnload;
    BOOLEAN             IrpCancel;
    PIRP                NewIrp;
    PIRP                RemoveLockIrp;
    KEVENT              Event;
    PFILE_OBJECT        KbdFileObject;
    PDEVICE_OBJECT      BttmDeviceObject;
    PDEVICE_OBJECT      KbdDeviceObject;
    ERESOURCE           Resource;
} KBDDRIVER_KEYBOARD_OBJECT, *PKBDDRIVER_KEYBOARD_OBJECT;

typedef struct _KBDDRIVER_DEVICE_EXTENSION
{
    KSPIN_LOCK          KbdObjSpinLock;
    LIST_ENTRY          KbdObjListHead;
    KSPIN_LOCK          ThreadListLock;
    LIST_ENTRY          ThreadListHead;
    PDRIVER_OBJECT      KbdDriverObject;
    
    // Przechowujemy config, żeby móc zrestartować sieć po wylogowaniu
    LPWSTR              RemoteIP;
    LPWSTR              RemotePort;
    
    BOOLEAN             IsUnloading;
} KBDDRIVER_DEVICE_EXTENSION, *PKBDDRIVER_DEVICE_EXTENSION;

// --- DEKLARACJE FUNKCJI ---

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath);
VOID KbdDriver_Unload(_In_ PDRIVER_OBJECT DriverObject);

NTSTATUS ObReferenceObjectByName(
    _In_ PUNICODE_STRING ObjectName,
    _In_ ULONG Attributes,
    _In_opt_ PACCESS_STATE AccessState,
    _In_opt_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _Out_ PVOID* Object
);