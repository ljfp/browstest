#ifndef SOCKS_SERVER_H
#define SOCKS_SERVER_H

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

// Constants
#define MAX_CONNECTIONS 64
#define BUFFER_SIZE 4096
#define SOCKS_PORT 1080
#define VIRTIO_DEVICE "\\\\.\\COM1"  // Adjust as needed for your virtio-serial port

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