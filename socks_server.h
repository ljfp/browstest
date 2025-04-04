#ifndef SOCKS_SERVER_H
#define SOCKS_SERVER_H

// Define WIN32_LEAN_AND_MEAN to prevent windows.h from including winsock.h
#define WIN32_LEAN_AND_MEAN

// Include headers in correct order
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>  // For AcceptEx and related functions
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <setupapi.h>  // For enumerating devices
#include <winreg.h>    // For registry functions
#include <cfgmgr32.h>  // For CM_ functions

// Link against required libraries
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")  // Required for AcceptEx
#pragma comment(lib, "setupapi.lib") // For device enumeration
#pragma comment(lib, "advapi32.lib") // For registry functions
#pragma comment(lib, "cfgmgr32.lib") // For configuration manager functions

// Constants
#define MAX_CONNECTIONS 64
#define BUFFER_SIZE 4096
#define SOCKS_PORT 1080

// Define multiple possible VirtIO device paths to try in sequence
#define VIRTIO_PATHS_COUNT 7
static const char* VIRTIO_PATHS[VIRTIO_PATHS_COUNT] = {
    "\\\\.\\\\GLOBALROOT\\Device\\VirtIO\\{74b3b5d0-e2fc-4dda-ad3d-c1a47a988e30}\\", // Red Hat VirtIO specific path
    "\\\\.\\\\Device\\VirtIO\\{74b3b5d0-e2fc-4dda-ad3d-c1a47a988e30}\\", // Alternative format
    "\\\\.\\Global\\com.redhat.spice.0",  // Standard virtio-serial name
    "\\\\.\\com.redhat.spice.0",          // Alternative format
    "\\\\.\\vport0p1",                    // Direct virtio port device
    "\\\\.\\COM2",                        // Try COM2-COM4 (not COM1 which is likely the standard serial)
    "\\\\.\\COM3"  
};

// Function to find VirtIO serial device (declaration)
HANDLE FindVirtIOSerialDevice(void);

// SOCKS protocol constants
#define SOCKS_VERSION 5
#define SOCKS_AUTH_NONE 0x00
#define SOCKS_CMD_CONNECT 0x01
#define SOCKS_ATYP_IPV4 0x01
#define SOCKS_ATYP_DOMAIN 0x03
#define SOCKS_ATYP_IPV6 0x04
#define SOCKS_REPLY_SUCCESS 0x00

// Operation types
typedef enum {
    OP_ACCEPT,
    OP_READ,
    OP_WRITE,
    OP_VIRTIO_READ,
    OP_VIRTIO_WRITE
} OP_TYPE;

// Connection state
typedef enum {
    STATE_INIT,
    STATE_AUTH,
    STATE_REQUEST,
    STATE_CONNECTED,
    STATE_CLOSING
} CONN_STATE;

// Per-connection context
typedef struct {
    SOCKET socket;
    CONN_STATE state;
    uint8_t buffer[BUFFER_SIZE];
    WSABUF wsaBuf;
    DWORD bytesTransferred;
    int connId;
    bool inUse;
    OP_TYPE pendingOp;
    OVERLAPPED overlap;
} CONNECTION_CONTEXT;

// Virtio message header for multiplexing
typedef struct {
    uint16_t connId;
    uint16_t length;
} VIRTIO_MSG_HEADER;

// Global data
extern HANDLE g_iocp;
extern HANDLE g_virtioHandle;
extern CONNECTION_CONTEXT g_connections[MAX_CONNECTIONS];
extern SOCKET g_listenSocket;
extern LPFN_ACCEPTEX lpfnAcceptEx;  // Add explicit declaration for AcceptEx function pointer

// Function prototypes
bool InitializeServer(void);
void CleanupServer(void);
bool InitializeVirtio(void);
int GetFreeConnectionSlot(void);
void CloseConnection(CONNECTION_CONTEXT* ctx);
bool HandleNewConnection(SOCKET clientSocket);
bool ProcessSocksAuth(CONNECTION_CONTEXT* ctx);
bool ProcessSocksRequest(CONNECTION_CONTEXT* ctx);
bool SendToVirtio(CONNECTION_CONTEXT* ctx, const uint8_t* data, uint16_t length);
bool ReceiveFromVirtio(void);
void PostAccept(void);
void PostClientRead(CONNECTION_CONTEXT* ctx);
void PostVirtioRead(void);

#endif // SOCKS_SERVER_H 