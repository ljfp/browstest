#include "socks_server.h"

// Global variables
HANDLE g_iocp = NULL;
HANDLE g_virtioHandle = NULL;
CONNECTION_CONTEXT g_connections[MAX_CONNECTIONS] = {0};
SOCKET g_listenSocket = INVALID_SOCKET;
LPFN_ACCEPTEX lpfnAcceptEx = NULL;

// Acceptance information for AcceptEx
SOCKET g_acceptSocket = INVALID_SOCKET;
char g_acceptBuffer[2 * (sizeof(SOCKADDR_IN) + 16)];
OVERLAPPED g_acceptOverlap = {0};

// Virtio read buffer and overlapped structure
uint8_t g_virtioReadBuffer[BUFFER_SIZE + sizeof(VIRTIO_MSG_HEADER)];
OVERLAPPED g_virtioReadOverlap = {0};

int main(void) {
    WSADATA wsaData;
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    OVERLAPPED* pOverlapped;
    CONNECTION_CONTEXT* ctx;
    int i;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // Initialize the server
    if (!InitializeServer()) {
        WSACleanup();
        return 1;
    }

    // Initialize virtio-serial connection
    if (!InitializeVirtio()) {
        CleanupServer();
        WSACleanup();
        return 1;
    }

    // Initialize connection contexts
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        g_connections[i].socket = INVALID_SOCKET;
        g_connections[i].inUse = false;
        g_connections[i].connId = i;
    }

    // Post an initial accept
    PostAccept();

    // Post an initial virtio read
    PostVirtioRead();

    printf("SOCKS server started. Listening on port %d\n", SOCKS_PORT);

    // Main event loop
    while (true) {
        if (!GetQueuedCompletionStatus(g_iocp, &bytesTransferred, &completionKey, &pOverlapped, INFINITE)) {
            if (pOverlapped == NULL) {
                // IOCP error
                printf("IOCP error: %d\n", GetLastError());
                break;
            }
        }

        if (pOverlapped == &g_acceptOverlap) {
            // New connection accepted
            SOCKET clientSocket = g_acceptSocket;
            g_acceptSocket = INVALID_SOCKET;

            // Create a new socket for the next accept
            PostAccept();

            // Handle the new connection
            if (!HandleNewConnection(clientSocket)) {
                closesocket(clientSocket);
            }
        }
        else if (pOverlapped == &g_virtioReadOverlap) {
            // Data received from virtio-serial
            if (bytesTransferred >= sizeof(VIRTIO_MSG_HEADER)) {
                VIRTIO_MSG_HEADER* header = (VIRTIO_MSG_HEADER*)g_virtioReadBuffer;
                uint16_t connId = header->connId;
                uint16_t length = header->length;

                if (connId < MAX_CONNECTIONS && g_connections[connId].inUse && 
                    bytesTransferred >= sizeof(VIRTIO_MSG_HEADER) + length) {
                    // Send data to the client
                    ctx = &g_connections[connId];
                    ctx->wsaBuf.buf = (char*)(g_virtioReadBuffer + sizeof(VIRTIO_MSG_HEADER));
                    ctx->wsaBuf.len = length;
                    ctx->pendingOp = OP_WRITE;

                    if (WSASend(ctx->socket, &ctx->wsaBuf, 1, NULL, 0, &ctx->overlap, NULL) == SOCKET_ERROR) {
                        if (WSAGetLastError() != WSA_IO_PENDING) {
                            CloseConnection(ctx);
                        }
                    }
                }
            }

            // Post another virtio read
            PostVirtioRead();
        }
        else {
            // Client socket operation completed
            ctx = CONTAINING_RECORD(pOverlapped, CONNECTION_CONTEXT, overlap);

            if (bytesTransferred == 0 && (ctx->pendingOp == OP_READ || ctx->pendingOp == OP_WRITE)) {
                // Connection closed by client
                CloseConnection(ctx);
                continue;
            }

            ctx->bytesTransferred = bytesTransferred;

            switch (ctx->pendingOp) {
                case OP_READ:
                    switch (ctx->state) {
                        case STATE_INIT:
                            if (ProcessSocksAuth(ctx)) {
                                ctx->state = STATE_AUTH;
                                PostClientRead(ctx);
                            } else {
                                CloseConnection(ctx);
                            }
                            break;
                        case STATE_AUTH:
                            if (ProcessSocksRequest(ctx)) {
                                ctx->state = STATE_CONNECTED;
                                PostClientRead(ctx);
                            } else {
                                CloseConnection(ctx);
                            }
                            break;
                        case STATE_CONNECTED:
                            // Forward data to virtio
                            if (!SendToVirtio(ctx, ctx->buffer, (uint16_t)bytesTransferred)) {
                                CloseConnection(ctx);
                            } else {
                                PostClientRead(ctx);
                            }
                            break;
                        default:
                            CloseConnection(ctx);
                            break;
                    }
                    break;
                case OP_WRITE:
                    // Ready to read more data
                    PostClientRead(ctx);
                    break;
                default:
                    break;
            }
        }
    }

    CleanupServer();
    WSACleanup();
    return 0;
}

bool InitializeServer(void) {
    struct sockaddr_in serverAddr;
    SOCKET listenSocket;
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytesReturned;

    // Create IOCP
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_iocp == NULL) {
        printf("CreateIoCompletionPort failed: %d\n", GetLastError());
        return false;
    }

    // Create listening socket
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        CloseHandle(g_iocp);
        g_iocp = NULL;
        return false;
    }

    // Associate socket with IOCP
    if (CreateIoCompletionPort((HANDLE)listenSocket, g_iocp, 0, 0) == NULL) {
        printf("Failed to associate socket with IOCP: %d\n", GetLastError());
        closesocket(listenSocket);
        CloseHandle(g_iocp);
        g_iocp = NULL;
        return false;
    }

    // Setup server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(SOCKS_PORT);

    // Bind the socket
    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        CloseHandle(g_iocp);
        g_iocp = NULL;
        return false;
    }

    // Start listening
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        CloseHandle(g_iocp);
        g_iocp = NULL;
        return false;
    }

    // Get AcceptEx function pointer
    if (WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, 
                 &guidAcceptEx, sizeof(guidAcceptEx), 
                 &lpfnAcceptEx, sizeof(lpfnAcceptEx), 
                 &bytesReturned, NULL, NULL) == SOCKET_ERROR) {
        printf("WSAIoctl failed: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        CloseHandle(g_iocp);
        g_iocp = NULL;
        return false;
    }

    g_listenSocket = listenSocket;
    return true;
}

bool InitializeVirtio(void) {
    // Open the virtio-serial device
    g_virtioHandle = CreateFile(
        VIRTIO_DEVICE,
        GENERIC_READ | GENERIC_WRITE,
        0,                          // No sharing
        NULL,                       // Default security
        OPEN_EXISTING,              // Open existing device
        FILE_FLAG_OVERLAPPED,       // Use overlapped I/O
        NULL                        // No template
    );

    if (g_virtioHandle == INVALID_HANDLE_VALUE) {
        printf("Failed to open virtio device: %d\n", GetLastError());
        return false;
    }

    // Associate virtio handle with IOCP
    if (CreateIoCompletionPort(g_virtioHandle, g_iocp, 0, 0) == NULL) {
        printf("Failed to associate virtio with IOCP: %d\n", GetLastError());
        CloseHandle(g_virtioHandle);
        g_virtioHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    return true;
}

void CleanupServer(void) {
    int i;

    // Close all connections
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        if (g_connections[i].inUse) {
            CloseConnection(&g_connections[i]);
        }
    }

    // Close listening socket
    if (g_listenSocket != INVALID_SOCKET) {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }

    // Close accept socket if open
    if (g_acceptSocket != INVALID_SOCKET) {
        closesocket(g_acceptSocket);
        g_acceptSocket = INVALID_SOCKET;
    }

    // Close virtio handle
    if (g_virtioHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_virtioHandle);
        g_virtioHandle = INVALID_HANDLE_VALUE;
    }

    // Close IOCP handle
    if (g_iocp != NULL) {
        CloseHandle(g_iocp);
        g_iocp = NULL;
    }
}

void PostAccept(void) {
    DWORD bytesReceived = 0;

    // Create a new socket for the next client
    g_acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_acceptSocket == INVALID_SOCKET) {
        printf("Failed to create accept socket: %d\n", WSAGetLastError());
        return;
    }

    // Reset the overlapped structure
    memset(&g_acceptOverlap, 0, sizeof(OVERLAPPED));

    // Post AcceptEx
    if (!lpfnAcceptEx(g_listenSocket, g_acceptSocket, g_acceptBuffer, 0,
                    sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                    &bytesReceived, &g_acceptOverlap)) {
        if (WSAGetLastError() != ERROR_IO_PENDING) {
            printf("AcceptEx failed: %d\n", WSAGetLastError());
            closesocket(g_acceptSocket);
            g_acceptSocket = INVALID_SOCKET;
        }
    }
}

void PostVirtioRead(void) {
    DWORD bytesRead;
    BOOL result;

    // Reset the overlapped structure
    memset(&g_virtioReadOverlap, 0, sizeof(OVERLAPPED));

    // Post ReadFile on virtio device
    result = ReadFile(
        g_virtioHandle,
        g_virtioReadBuffer,
        sizeof(g_virtioReadBuffer),
        &bytesRead,
        &g_virtioReadOverlap
    );

    if (!result && GetLastError() != ERROR_IO_PENDING) {
        printf("Failed to post virtio read: %d\n", GetLastError());
    }
}

void PostClientRead(CONNECTION_CONTEXT* ctx) {
    DWORD flags = 0;
    int result;

    // Setup the overlapped structure
    memset(&ctx->overlap, 0, sizeof(OVERLAPPED));
    
    // Setup the buffer
    ctx->wsaBuf.buf = (char*)ctx->buffer;
    ctx->wsaBuf.len = BUFFER_SIZE;
    ctx->pendingOp = OP_READ;

    // Post WSARecv
    result = WSARecv(
        ctx->socket,
        &ctx->wsaBuf,
        1,
        NULL,
        &flags,
        &ctx->overlap,
        NULL
    );

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        printf("WSARecv failed: %d\n", WSAGetLastError());
        CloseConnection(ctx);
    }
}

int GetFreeConnectionSlot(void) {
    int i;
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        if (!g_connections[i].inUse) {
            return i;
        }
    }
    return -1;
}

bool HandleNewConnection(SOCKET clientSocket) {
    int slot = GetFreeConnectionSlot();
    CONNECTION_CONTEXT* ctx;

    if (slot == -1) {
        printf("Max connections reached\n");
        return false;
    }

    // Associate socket with IOCP
    if (CreateIoCompletionPort((HANDLE)clientSocket, g_iocp, (ULONG_PTR)&g_connections[slot], 0) == NULL) {
        printf("Failed to associate client socket with IOCP: %d\n", GetLastError());
        return false;
    }

    // Initialize connection context
    ctx = &g_connections[slot];
    ctx->socket = clientSocket;
    ctx->inUse = true;
    ctx->state = STATE_INIT;
    memset(&ctx->overlap, 0, sizeof(OVERLAPPED));

    // Post initial read to receive SOCKS handshake
    PostClientRead(ctx);
    
    return true;
}

void CloseConnection(CONNECTION_CONTEXT* ctx) {
    if (!ctx->inUse) {
        return;
    }

    closesocket(ctx->socket);
    ctx->socket = INVALID_SOCKET;
    ctx->inUse = false;
}

bool ProcessSocksAuth(CONNECTION_CONTEXT* ctx) {
    uint8_t ver, nmethods, i;
    uint8_t response[2];
    WSABUF wsaBuf;
    DWORD bytesSent;

    if (ctx->bytesTransferred < 2) {
        printf("Invalid SOCKS auth packet (too short)\n");
        return false;
    }

    ver = ctx->buffer[0];
    nmethods = ctx->buffer[1];

    if (ver != SOCKS_VERSION) {
        printf("Unsupported SOCKS version: %d\n", ver);
        return false;
    }

    if (ctx->bytesTransferred < 2 + nmethods) {
        printf("Invalid SOCKS auth packet (methods truncated)\n");
        return false;
    }

    // Check if no-auth method is supported
    for (i = 0; i < nmethods; i++) {
        if (ctx->buffer[2 + i] == SOCKS_AUTH_NONE) {
            // Send auth response
            response[0] = SOCKS_VERSION;
            response[1] = SOCKS_AUTH_NONE;

            wsaBuf.buf = (char*)response;
            wsaBuf.len = 2;

            if (WSASend(ctx->socket, &wsaBuf, 1, &bytesSent, 0, NULL, NULL) == SOCKET_ERROR) {
                printf("Failed to send auth response: %d\n", WSAGetLastError());
                return false;
            }

            return true;
        }
    }

    printf("No supported auth methods\n");
    return false;
}

bool ProcessSocksRequest(CONNECTION_CONTEXT* ctx) {
    uint8_t ver, cmd, rsv, atyp;
    uint8_t response[10]; // We'll use IPv4 response format for simplicity
    WSABUF wsaBuf;
    DWORD bytesSent;
    uint16_t port;
    char addrBuf[256];
    int addrLen = 0;

    if (ctx->bytesTransferred < 4) {
        printf("Invalid SOCKS request (too short)\n");
        return false;
    }

    ver = ctx->buffer[0];
    cmd = ctx->buffer[1];
    rsv = ctx->buffer[2];
    atyp = ctx->buffer[3];

    if (ver != SOCKS_VERSION) {
        printf("Unsupported SOCKS version in request: %d\n", ver);
        return false;
    }

    if (cmd != SOCKS_CMD_CONNECT) {
        printf("Unsupported SOCKS command: %d\n", cmd);
        return false;
    }

    // Extract address based on address type
    switch (atyp) {
        case SOCKS_ATYP_IPV4:
            if (ctx->bytesTransferred < 10) {
                printf("Invalid IPv4 request (too short)\n");
                return false;
            }
            sprintf(addrBuf, "%d.%d.%d.%d", 
                ctx->buffer[4], ctx->buffer[5], ctx->buffer[6], ctx->buffer[7]);
            port = (ctx->buffer[8] << 8) | ctx->buffer[9];
            addrLen = 4;
            break;
        case SOCKS_ATYP_DOMAIN:
            if (ctx->bytesTransferred < 5) {
                printf("Invalid domain request (too short)\n");
                return false;
            }
            addrLen = ctx->buffer[4];
            if (ctx->bytesTransferred < 5 + addrLen + 2) {
                printf("Invalid domain request (domain truncated)\n");
                return false;
            }
            memcpy(addrBuf, &ctx->buffer[5], addrLen);
            addrBuf[addrLen] = '\0';
            port = (ctx->buffer[5 + addrLen] << 8) | ctx->buffer[5 + addrLen + 1];
            break;
        case SOCKS_ATYP_IPV6:
            printf("IPv6 not supported in this implementation\n");
            return false;
        default:
            printf("Unsupported address type: %d\n", atyp);
            return false;
    }

    printf("SOCKS request: Connect to %s:%d\n", addrBuf, port);

    // Send connection request to virtio
    // Format: [conn_id][atyp][addr_len][addr][port]
    uint8_t reqBuf[4 + 256 + 2]; // 4 bytes header + max addr + port
    uint16_t reqLen = 0;
    
    reqBuf[reqLen++] = atyp;
    
    if (atyp == SOCKS_ATYP_DOMAIN) {
        reqBuf[reqLen++] = (uint8_t)addrLen;
        memcpy(&reqBuf[reqLen], addrBuf, addrLen);
        reqLen += addrLen;
    } else { // IPv4
        memcpy(&reqBuf[reqLen], &ctx->buffer[4], 4);
        reqLen += 4;
    }
    
    // Copy port
    reqBuf[reqLen++] = (port >> 8) & 0xFF;
    reqBuf[reqLen++] = port & 0xFF;
    
    if (!SendToVirtio(ctx, reqBuf, reqLen)) {
        printf("Failed to send connection request to virtio\n");
        return false;
    }

    // Send success response
    response[0] = SOCKS_VERSION;
    response[1] = SOCKS_REPLY_SUCCESS;
    response[2] = 0; // RSV
    response[3] = SOCKS_ATYP_IPV4;
    // Use all zeros for the bound address and port for simplicity
    memset(&response[4], 0, 6);

    wsaBuf.buf = (char*)response;
    wsaBuf.len = 10;

    if (WSASend(ctx->socket, &wsaBuf, 1, &bytesSent, 0, NULL, NULL) == SOCKET_ERROR) {
        printf("Failed to send SOCKS response: %d\n", WSAGetLastError());
        return false;
    }

    return true;
}

bool SendToVirtio(CONNECTION_CONTEXT* ctx, const uint8_t* data, uint16_t length) {
    uint8_t buffer[BUFFER_SIZE + sizeof(VIRTIO_MSG_HEADER)];
    VIRTIO_MSG_HEADER* header = (VIRTIO_MSG_HEADER*)buffer;
    DWORD bytesWritten;
    OVERLAPPED overlap = {0};
    BOOL result;

    if (length > BUFFER_SIZE) {
        printf("Data too large for virtio buffer\n");
        return false;
    }

    // Prepare message header
    header->connId = (uint16_t)ctx->connId;
    header->length = length;

    // Copy data after header
    memcpy(buffer + sizeof(VIRTIO_MSG_HEADER), data, length);

    // Write to virtio synchronously for simplicity
    result = WriteFile(
        g_virtioHandle,
        buffer,
        sizeof(VIRTIO_MSG_HEADER) + length,
        &bytesWritten,
        &overlap
    );

    if (!result) {
        if (GetLastError() == ERROR_IO_PENDING) {
            // Wait for the write to complete
            if (!GetOverlappedResult(g_virtioHandle, &overlap, &bytesWritten, TRUE)) {
                printf("WriteFile to virtio failed: %d\n", GetLastError());
                return false;
            }
        } else {
            printf("WriteFile to virtio failed: %d\n", GetLastError());
            return false;
        }
    }

    return (bytesWritten == sizeof(VIRTIO_MSG_HEADER) + length);
} 