/*++
Module Name: network.h
Abstract: WSK interface declarations.
Environment: Kernel mode only.
--*/

#pragma once
#include "driver.h"
#include <wsk.h>

#define SOCKET ULONG_PTR
extern SOCKET ClientSocket;
extern KSPIN_LOCK g_SocketLock;

#ifndef WSK_INVALID_SOCKET
    #define WSK_INVALID_SOCKET ((SOCKET)(~0))
#endif

typedef struct _WSKDATA { UINT16 HighestVersion; UINT16 LowestVersion; } WSKDATA, *PWSKDATA;
typedef struct _WSKOVERLAPPED { ULONG_PTR Internal; ULONG_PTR InternalHigh; union { struct { ULONG Offset; ULONG OffsetHigh; } DUMMYSTRUCTNAME; PVOID Pointer; } DUMMYUNIONNAME; KEVENT Event; } WSKOVERLAPPED, *PWSKOVERLAPPED;
typedef VOID (*LPWSKOVERLAPPED_COMPLETION_ROUTINE)(_In_ NTSTATUS Status, _In_ ULONG_PTR BytesTransferred, _In_ WSKOVERLAPPED* Overlapped);

NTSTATUS NetClient_Initialize(_In_opt_ LPCWSTR NodeName, _In_opt_ LPCWSTR ServiceName, _In_ ADDRESS_FAMILY AddressFamily, _In_ USHORT SocketType);
VOID NetClient_Cleanup(VOID);
VOID NetClient_FullCleanup(VOID); // <--- To jest nowe, ważne dla driver.c

// WSK Core Wrappers
VOID WSKAPI WSKSetLastError(_In_ NTSTATUS Status);
NTSTATUS WSKAPI WSKGetLastError(VOID);
NTSTATUS WSKAPI WSKStartup(_In_ UINT16 Version, _Out_ WSKDATA* WSKData);
VOID WSKAPI WSKCleanup(VOID);
NTSTATUS WSKAPI WSKGetAddrInfo(_In_opt_ LPCWSTR NodeName, _In_opt_ LPCWSTR ServiceName, _In_ UINT32 Namespace, _In_opt_ GUID* Provider, _In_opt_ PADDRINFOEXW Hints, _Outptr_result_maybenull_ PADDRINFOEXW* Result, _In_opt_ UINT32 TimeoutMilliseconds, _In_opt_ WSKOVERLAPPED* Overlapped, _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine);
VOID WSKAPI WSKFreeAddrInfo(_In_ PADDRINFOEXW Data);
NTSTATUS WSKAPI WSKGetNameInfo(_In_ const SOCKADDR* Address, _In_ ULONG AddressLength, _Out_writes_opt_(NodeNameSize) LPWSTR NodeName, _In_ ULONG NodeNameSize, _Out_writes_opt_(ServiceNameSize) LPWSTR ServiceName, _In_ ULONG ServiceNameSize, _In_ ULONG Flags);
NTSTATUS WSKAPI WSKSocket(_Out_ SOCKET* Socket, _In_ ADDRESS_FAMILY AddressFamily, _In_ USHORT SocketType, _In_ ULONG Protocol, _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor);
NTSTATUS WSKAPI WSKCloseSocket(_In_ SOCKET Socket);
NTSTATUS WSKAPI WSKIoctl(_In_ SOCKET Socket, _In_ ULONG ControlCode, _In_reads_bytes_opt_(InputSize) PVOID InputBuffer, _In_ SIZE_T InputSize, _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer, _In_ SIZE_T OutputSize, _Out_opt_ SIZE_T* OutputSizeReturned, _In_opt_ WSKOVERLAPPED* Overlapped, _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine);
NTSTATUS WSKAPI WSKSendTo(_In_ SOCKET Socket, _In_ PVOID Buffer, _In_ SIZE_T BufferLength, _Out_opt_ SIZE_T* NumberOfBytesSent, _Reserved_ ULONG Flags, _In_opt_ PSOCKADDR RemoteAddress, _In_ SIZE_T RemoteAddressLength, _In_opt_ WSKOVERLAPPED* Overlapped, _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine);