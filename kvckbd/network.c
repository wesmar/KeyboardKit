/*++
Module Name: network.c
Abstract: WSK client implementation for UDP communication with thread-safe socket access
Environment: Kernel mode only.
--*/

#include "network.h"

// Memory Pool Tag for Network Operations
#define NETWORK_POOL_TAG    'kteN'  // 'Netk' in little-endian

// Global Network State (protected by spinlock)
SOCKET      ClientSocket = 0;
KSPIN_LOCK  g_SocketLock;
LPWSTR      g_HostName = NULL;
LPWSTR      g_PortName = NULL;
PADDRINFOEXW g_AddrInfo = NULL;

NTSTATUS 
NetClient_Initialize(
    _In_opt_ LPCWSTR        NodeName,
    _In_opt_ LPCWSTR        ServiceName,
    _In_     ADDRESS_FAMILY AddressFamily,
    _In_     USHORT         SocketType
)
{
    NTSTATUS        Status;
    ADDRINFOEXW     Hints;
    PADDRINFOEXW    CurrentAddr;
    SOCKET          TempSocket;
    
    Status = STATUS_SUCCESS;
    TempSocket = 0;
    
    // Initialize socket spinlock
    KeInitializeSpinLock(&g_SocketLock);
    
    // Allocate buffers for host and port name strings
    g_HostName = (LPWSTR)ExAllocatePoolZero(
        PagedPool,
        NI_MAXHOST * sizeof(WCHAR),
        NETWORK_POOL_TAG
    );
    
    g_PortName = (LPWSTR)ExAllocatePoolZero(
        PagedPool,
        NI_MAXSERV * sizeof(WCHAR),
        NETWORK_POOL_TAG
    );
    
    if (g_HostName == NULL || g_PortName == NULL)
    {
        KBD_ERROR("NetClient_Initialize: Failed to allocate name buffers");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    
    // Initialize address resolution hints
    RtlZeroMemory(&Hints, sizeof(ADDRINFOEXW));
    Hints.ai_family = AddressFamily;
    Hints.ai_socktype = SocketType;
    Hints.ai_protocol = (SocketType == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;
    
    // Resolve the remote address
    Status = WSKGetAddrInfo(
        NodeName,
        ServiceName,
        NS_ALL,
        NULL,
        &Hints,
        &g_AddrInfo,
        WSK_INFINITE_WAIT,
        NULL,
        NULL
    );
    
    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("WSKGetAddrInfo failed: 0x%08X", Status);
        goto Cleanup;
    }
    
    // Verify we got at least one address
    if (g_AddrInfo == NULL)
    {
        KBD_ERROR("Server name '%ws' could not be resolved", NodeName);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    
    // Iterate through resolved addresses and create socket
    for (CurrentAddr = g_AddrInfo; CurrentAddr != NULL; CurrentAddr = CurrentAddr->ai_next)
    {
        // Create socket
        Status = WSKSocket(
            &TempSocket,
            (ADDRESS_FAMILY)CurrentAddr->ai_family,
            (USHORT)CurrentAddr->ai_socktype,
            CurrentAddr->ai_protocol,
            NULL
        );
        
        if (!NT_SUCCESS(Status))
        {
            KBD_ERROR("WSKSocket failed: 0x%08X", Status);
            continue;
        }
        
        // Get printable address and port
        Status = WSKGetNameInfo(
            CurrentAddr->ai_addr,
            (ULONG)CurrentAddr->ai_addrlen,
            g_HostName,
            NI_MAXHOST,
            g_PortName,
            NI_MAXSERV,
            NI_NUMERICHOST | NI_NUMERICSERV
        );
        
        if (!NT_SUCCESS(Status))
        {
            KBD_ERROR("WSKGetNameInfo failed: 0x%08X", Status);
            WSKCloseSocket(TempSocket);
            TempSocket = 0;
            continue;
        }
        
        KBD_INFO("Attempting connection to %ws:%ws", g_HostName, g_PortName);
        
        // For UDP sockets, configure the default remote address
        if (CurrentAddr->ai_socktype == SOCK_DGRAM)
        {
            Status = WSKIoctl(
                TempSocket,
                SIO_WSK_SET_SENDTO_ADDRESS,
                CurrentAddr->ai_addr,
                CurrentAddr->ai_addrlen,
                NULL,
                0,
                NULL,
                NULL,
                NULL
            );
            
            if (!NT_SUCCESS(Status))
            {
                KBD_ERROR("WSKIoctl SIO_WSK_SET_SENDTO_ADDRESS failed: 0x%08X", Status);
                WSKCloseSocket(TempSocket);
                TempSocket = 0;
                continue;
            }
        }
        
        // Socket successfully configured - store it atomically
        InterlockedExchange64((LONG64*)&ClientSocket, (LONG64)TempSocket);
        break;
    }
    
    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("Unable to establish connection to %ws:%ws", NodeName, ServiceName);
        goto Cleanup;
    }
    
    KBD_INFO("Network client initialized successfully");
    
Cleanup:
    
    // Clean up on failure
    if (!NT_SUCCESS(Status))
    {
        if (g_HostName != NULL)
        {
            ExFreePoolWithTag(g_HostName, NETWORK_POOL_TAG);
            g_HostName = NULL;
        }
        
        if (g_PortName != NULL)
        {
            ExFreePoolWithTag(g_PortName, NETWORK_POOL_TAG);
            g_PortName = NULL;
        }
        
        if (g_AddrInfo != NULL)
        {
            WSKFreeAddrInfo(g_AddrInfo);
            g_AddrInfo = NULL;
        }
        
        if (TempSocket != 0)
        {
            WSKCloseSocket(TempSocket);
        }
    }
    
    return Status;
}

VOID 
NetClient_Cleanup(
    VOID
)
{
    KIRQL OldIrql;
    SOCKET LocalSocket;
    
    // Atomically retrieve and clear the socket
    KeAcquireSpinLock(&g_SocketLock, &OldIrql);
    LocalSocket = ClientSocket;
    ClientSocket = 0;
    KeReleaseSpinLock(&g_SocketLock, OldIrql);
    
    // Free host name buffer
    if (g_HostName != NULL)
    {
        ExFreePoolWithTag(g_HostName, NETWORK_POOL_TAG);
        g_HostName = NULL;
    }
    
    // Free port name buffer
    if (g_PortName != NULL)
    {
        ExFreePoolWithTag(g_PortName, NETWORK_POOL_TAG);
        g_PortName = NULL;
    }
    
    // Free address information
    if (g_AddrInfo != NULL)
    {
        WSKFreeAddrInfo(g_AddrInfo);
        g_AddrInfo = NULL;
    }
    
    // Close socket outside of spinlock
    if (LocalSocket != 0)
    {
        WSKCloseSocket(LocalSocket);
    }
    
    KBD_INFO("Network client cleanup completed");
}