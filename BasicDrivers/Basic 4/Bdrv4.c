/*--

    Copyright (c) Santos Merino del Pozo.  All rights reserved.
    Use of this source code is governed by a MIT-style license which can be
    found in the LICENSE file.

    Module Name:

        Bdrv4.c

    Summary:

        This module extends "Basic 3" project, now calling PsCreateSystemThread
        in the IOCTL handler. This is to check whether using a NULL ProcessHand-
        le parameter, the new thread runs in the context of the System (expected)
        or the calling process (not expected). 

        See exercise 1 about Windows system threads (chapter 3) in the awesome 
        "Practical Reverse Engineering" book by Bruce Dang.

    Author:

        Santos Merino del Pozo (@santitox) - Initial version

    Contact:

        santos.research@gmail.com

--*/

#include "Bdrv4.h"

_Use_decl_annotations_
NTSTATUS
Bdrv4DefaultDispatch (
    _Inout_ PDEVICE_OBJECT pDeviceObject,
    _Inout_ PIRP pIrp
    )
{
    UNREFERENCED_PARAMETER(pDeviceObject);
    PAGED_CODE();

    //
    // Set the IRP status information, indicating either success
    // or a hardware failure. In this case (where no additional 
    // processing is requried), result is always successful
    //
    pIrp->IoStatus.Status = STATUS_SUCCESS;

    //
    // We just indicate that all processing has been finished.
    // Thereby, the IO manager can check whether (higher-level)
    // drivers on the stack called IoCompletion. If so, they are
    // called until the top layer is reached, hence completing
    // the IRP
    //
    IoCompleteRequest(pIrp,
                      IO_NO_INCREMENT);

    DbgPrintEx(77, 0, "[DefaultDispatch]\n");

    return pIrp->IoStatus.Status;
}

_Use_decl_annotations_
NTSTATUS
Bdrv4DispatchDeviceControl (
    _Inout_ PDEVICE_OBJECT pDeviceObject,
    _Inout_ PIRP pIrp
    )
{
    UNREFERENCED_PARAMETER(pDeviceObject);
    PAGED_CODE();

    HANDLE hThread;
    HANDLE hHandle;
    NTSTATUS ntStatus;
    PIO_STACK_LOCATION pIoStackIrp;
    ULONG ulIoControlCode;

    //
    // When we get the IRP from a higher-level device in the chain,
    // we need to get a pointer to the corresponding stack pointer.
    // Do not confuse this term (referring to a private section of
    // memory within the IRP) with the ESP/RSP
    //
    pIoStackIrp = NULL;
    pIoStackIrp = IoGetCurrentIrpStackLocation(pIrp);

    if (!pIoStackIrp)
    {
        DbgPrintEx(77, 0, "[Bdrv4DispatchDeviceControl] - "
                           "Failed to get a pointer to the IO stack of the caller\n");
        return STATUS_INTERNAL_ERROR;
    }

    //
    // Identify the type of request (i.e., the IOCTL)
    //
    ulIoControlCode = pIoStackIrp->Parameters.DeviceIoControl.IoControlCode;

    if (ulIoControlCode != IOCTL_Device_Function)
    {
        DbgPrintEx(77, 0, "[Bdrv4DispatchDeviceControl] - "
                          "IOCTL not supported\n");
        return STATUS_NOT_SUPPORTED;
    }

    DbgPrintEx(77, 0, "[Bdrv4DispatchDeviceControl]\n");

    //
    // Call the IOCTL handler using a system thread
    //
    hThread = NULL;

    //
    // To run on SYSTEM context
    //
    hHandle = NULL;

    //
    // To run on current process context (i.e., the caller process
    // sending the IOCTL to the device)
    //
    // hHandle = ZwCurrentProcess();

    ntStatus = PsCreateSystemThread(&hThread,
                                    THREAD_ALL_ACCESS,
                                    NULL,
                                    hHandle,
                                    NULL,
                                    Bdrv4HandleIoctlSayHello,
                                    NULL);

    if (!NT_SUCCESS(ntStatus))
    {
        DbgPrintEx(77, 0, "[Bdrv4DispatchDeviceControl] - "
            "PsCreateSystemThread() failed (%.8Xh)\n", ntStatus);
        return ntStatus;
    }

    ZwClose(hThread);

    pIrp->IoStatus.Status = ntStatus;
    IoCompleteRequest(pIrp,
                      IO_NO_INCREMENT);

    return ntStatus;
}

_Use_decl_annotations_
VOID
Bdrv4HandleIoctlSayHello (
    _In_ PVOID pStartContext
    )
{
    UNREFERENCED_PARAMETER(pStartContext);
    
    //
    // Set an execution interval of 5 seconds
    //
    LARGE_INTEGER Timeout = { 0 };
    Timeout.QuadPart = RELATIVE(SECONDS(5));
    for (UINT8 TicToc = 0; TicToc < 20; TicToc++)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
        DbgPrintEx(77, 0, "[Bdrv4HandleIoctlSayHello] - Hello!\n");
    }

    //
    // Terminate the system thread at this point, hence the
    // function will not return
    //
    PsTerminateSystemThread(STATUS_SUCCESS);
}

_Use_decl_annotations_
VOID
Bdrv4DriverUnload (
    _In_ PDRIVER_OBJECT pDriverObject
    )
{
    PAGED_CODE();

    UNICODE_STRING szUDeviceLink;

    //
    // The Unload routine performs any operations that are
    // necessary before the system unloads the driver. In this case
    // we need to delete the symbolic link and the corresponding
    // device (both are created when starting the driver)
    //
    RtlInitUnicodeString(&szUDeviceLink,
                         g_pszDeviceLink);
    IoDeleteSymbolicLink(&szUDeviceLink);
    IoDeleteDevice(pDriverObject->DeviceObject);

    DbgPrintEx(77, 0, "[Bdrv4DriverUnload] - Device and symlink deleted\n");
}

_Use_decl_annotations_
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT pDriverObject,
    _In_ PUNICODE_STRING pRegistryPath
    )
{
    UNREFERENCED_PARAMETER(pRegistryPath);
    PAGED_CODE();

    NTSTATUS ntStatus;
    PDEVICE_OBJECT pDeviceObject;
    UNICODE_STRING szUDeviceName;
    UNICODE_STRING szUDeviceLink;

    RtlInitUnicodeString(&szUDeviceName,
                         g_pszDeviceName);
    RtlInitUnicodeString(&szUDeviceLink,
                         g_pszDeviceLink);

    //
    // Create the device
    //
    ntStatus = IoCreateDevice(pDriverObject,
                              0,
                              &szUDeviceName,
                              FILE_DEVICE_BASIC,
                              0,
                              TRUE,
                              &pDeviceObject);

    if (!NT_SUCCESS(ntStatus))
    {
        DbgPrintEx(77, 0, "[DriverEntry] - Failed to create device (%.8Xh)\n",
                   ntStatus);
        return ntStatus;
    }

    //
    // Create the symbolic link
    //
    ntStatus = IoCreateSymbolicLink(&szUDeviceLink,
                                    &szUDeviceName);

    if (!NT_SUCCESS(ntStatus))
    {
        IoDeleteDevice(pDriverObject->DeviceObject);
        DbgPrintEx(77, 0, "[DriverEntry] - Failed to create symlink (%.8Xh)\n",
                   ntStatus);
        return ntStatus;
    }

    //
    // Set default unload function called when stopping the corresponding
    // service (e.g., sc stop from an instance of the command interpreter Cmd.exe)
    //
    pDriverObject->DriverUnload = Bdrv4DriverUnload;

    //
    // We assign the same default dispatch routine to all IRP function
    // codes. Otherwise, If we don't do this any (unrecognized) IRP
    // will result in STATUS_INVALID_DEVICE_REQUEST which is a bad
    // programming practice. As suggested by the MSDN documentation,
    // a default dispatch routine should forward the IRP to the next
    // (lower-level) driver
    //
    for (ULONG i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        //
        // We just complete all IRPs that come its way.
        // However, as there is no user-mode application communicating with
        // the driver, no debug output corresponding with DefaultDispatch
        // should be seen. For a more elaborated driver, see "Basic 3" which
        // includes a user-land application sending IOCTLs to the driver
        //
        pDriverObject->MajorFunction[i] = Bdrv4DefaultDispatch;
    }

    //
    // We set a different dispatch function for IRP_MJ_DEVICE_CONTROL, hence
    // allowing us to process IOCTLs coming from a user-mode application that
    // has opened the corresponding device (i.e., basicDevice in this case).
    // See project "Basic 3 (User)" for an example of such application
    //
    pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Bdrv4DispatchDeviceControl;

    DbgPrintEx(77, 0, "[DriverEntry] - Device and symlink created\n");

    return STATUS_SUCCESS;
}
// EoF