/*++
Module Name: network.c
Abstract: WSK client implementation.
Environment: Kernel mode only.
--*/

#include "network.h"

#define NETWORK_POOL_TAG    'kteN'

SOCKET      ClientSocket = 0;
KSPIN_LOCK  g_SocketLock;
LPWSTR      g_HostName = NULL;
LPWSTR      g_PortName = NULL;
PADDRINFOEXW g_AddrInfo = NULL;

NTSTATUS NetClient_Initialize(_In_opt_ LPCWSTR NodeName, _In_opt_ LPCWSTR ServiceName, _In_ ADDRESS_FAMILY AddressFamily, _In_ USHORT SocketType)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ADDRINFOEXW Hints;
    PADDRINFOEXW CurrentAddr;
    SOCKET TempSocket = 0;

    KeInitializeSpinLock(&g_SocketLock);
    
    // Alokujemy bufory tylko jeśli są NULL (przydatne przy restarcie)
    if (g_HostName == NULL) {
        g_HostName = (LPWSTR)ExAllocatePoolZero(PagedPool, NI_MAXHOST * sizeof(WCHAR), NETWORK_POOL_TAG);
    }
    if (g_PortName == NULL) {
        g_PortName = (LPWSTR)ExAllocatePoolZero(PagedPool, NI_MAXSERV * sizeof(WCHAR), NETWORK_POOL_TAG);
    }

    if (g_HostName == NULL || g_PortName == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    
    RtlZeroMemory(&Hints, sizeof(ADDRINFOEXW));
    Hints.ai_family = AddressFamily;
    Hints.ai_socktype = SocketType;
    Hints.ai_protocol = (SocketType == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;

    Status = WSKGetAddrInfo(NodeName, ServiceName, NS_ALL, NULL, &Hints, &g_AddrInfo, WSK_INFINITE_WAIT, NULL, NULL);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    
    for (CurrentAddr = g_AddrInfo; CurrentAddr != NULL; CurrentAddr = CurrentAddr->ai_next) {
        Status = WSKSocket(&TempSocket, (ADDRESS_FAMILY)CurrentAddr->ai_family, (USHORT)CurrentAddr->ai_socktype, CurrentAddr->ai_protocol, NULL);
        if (!NT_SUCCESS(Status)) continue;
        
        Status = WSKGetNameInfo(CurrentAddr->ai_addr, (ULONG)CurrentAddr->ai_addrlen, g_HostName, NI_MAXHOST, g_PortName, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
        if (!NT_SUCCESS(Status)) { WSKCloseSocket(TempSocket); TempSocket = 0; continue; }
        
        if (CurrentAddr->ai_socktype == SOCK_DGRAM) {
            Status = WSKIoctl(TempSocket, SIO_WSK_SET_SENDTO_ADDRESS, CurrentAddr->ai_addr, CurrentAddr->ai_addrlen, NULL, 0, NULL, NULL, NULL);
            if (!NT_SUCCESS(Status)) { WSKCloseSocket(TempSocket); TempSocket = 0; continue; }
        }
        
        InterlockedExchange64((LONG64*)&ClientSocket, (LONG64)TempSocket);
        break;
    }
    
    if (!NT_SUCCESS(Status)) goto Cleanup;
    return STATUS_SUCCESS;

Cleanup:
    if (g_AddrInfo != NULL) { WSKFreeAddrInfo(g_AddrInfo); g_AddrInfo = NULL; }
    if (TempSocket != 0) WSKCloseSocket(TempSocket);
    return Status;
}

// Szybkie czyszczenie (tylko socket, bez zwalniania stringów - do restartu)
VOID NetClient_Cleanup(VOID)
{
    KIRQL OldIrql;
    SOCKET LocalSocket;

    KeAcquireSpinLock(&g_SocketLock, &OldIrql);
    LocalSocket = ClientSocket;
    ClientSocket = 0;
    KeReleaseSpinLock(&g_SocketLock, OldIrql);
    
    if (g_AddrInfo != NULL) { WSKFreeAddrInfo(g_AddrInfo); g_AddrInfo = NULL; }
    if (LocalSocket != 0) WSKCloseSocket(LocalSocket);
}

// Pełne czyszczenie (do Unload)
VOID NetClient_FullCleanup(VOID)
{
    NetClient_Cleanup();
    if (g_HostName != NULL) { ExFreePoolWithTag(g_HostName, NETWORK_POOL_TAG); g_HostName = NULL; }
    if (g_PortName != NULL) { ExFreePoolWithTag(g_PortName, NETWORK_POOL_TAG); g_PortName = NULL; }
}
