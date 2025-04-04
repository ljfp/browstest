// Define _POSIX_C_SOURCE for addrinfo structure
#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_CONNECTIONS 64
#define BUFFER_SIZE 4096
#define VIRTIO_DEVICE "/tmp/vserial"  // Adjust for your setup

// SOCKS protocol constants
#define SOCKS_ATYP_IPV4 0x01
#define SOCKS_ATYP_DOMAIN 0x03
#define SOCKS_ATYP_IPV6 0x04

// Virtio message header for multiplexing
typedef struct {
    uint16_t connId;
    uint16_t length;
} __attribute__((packed)) VIRTIO_MSG_HEADER;

// Connection state
typedef struct {
    int socket;
    bool inUse;
    uint16_t connId;
} CONNECTION_INFO;

// Global data
CONNECTION_INFO g_connections[MAX_CONNECTIONS] = {0};
int g_virtioFd = -1;

// Function prototypes
bool InitializeVirtio(void);
void CleanupVirtio(void);
bool HandleConnectionRequest(uint16_t connId, uint8_t* data, uint16_t length);
bool SendToVirtio(uint16_t connId, const uint8_t* data, uint16_t length);
void CloseConnection(CONNECTION_INFO* conn);

int main(void) {
    struct pollfd fds[MAX_CONNECTIONS + 1];
    int nfds;
    int i;
    uint8_t buffer[BUFFER_SIZE + sizeof(VIRTIO_MSG_HEADER)];
    
    // Initialize virtio connection
    if (!InitializeVirtio()) {
        return 1;
    }
    
    // Initialize all connections
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        g_connections[i].socket = -1;
        g_connections[i].inUse = false;
        g_connections[i].connId = i;
    }
    
    printf("Host proxy started. Waiting for connections...\n");
    
    while (1) {
        // Set up polling
        nfds = 0;
        
        // Add virtio device to poll
        fds[nfds].fd = g_virtioFd;
        fds[nfds].events = POLLIN;
        nfds++;
        
        // Add active connections to poll
        for (i = 0; i < MAX_CONNECTIONS; i++) {
            if (g_connections[i].inUse) {
                fds[nfds].fd = g_connections[i].socket;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }
        
        // Wait for events
        if (poll(fds, nfds, -1) <= 0) {
            perror("poll error");
            break;
        }
        
        // Check virtio device for data
        if (fds[0].revents & POLLIN) {
            ssize_t bytesRead = read(g_virtioFd, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                printf("Received %zd bytes from virtio device\n", bytesRead);
                
                // Debug: Display the first few bytes
                printf("First 16 bytes of data: ");
                for (int i = 0; i < (bytesRead < 16 ? bytesRead : 16); i++) {
                    printf("%02X ", buffer[i]);
                }
                printf("\n");
                
                if (bytesRead >= (ssize_t)sizeof(VIRTIO_MSG_HEADER)) {
                    VIRTIO_MSG_HEADER* header = (VIRTIO_MSG_HEADER*)buffer;
                    uint16_t connId = header->connId;
                    uint16_t length = header->length;
                    
                    printf("Virtio message: connId=%u, length=%u\n", connId, length);
                    
                    if (bytesRead >= (ssize_t)(sizeof(VIRTIO_MSG_HEADER) + length)) {
                        // Check if this is a new connection or data for an existing one
                        if (connId < MAX_CONNECTIONS) {
                            if (!g_connections[connId].inUse) {
                                // New connection request
                                HandleConnectionRequest(connId, buffer + sizeof(VIRTIO_MSG_HEADER), length);
                            } else {
                                // Data for existing connection
                                if (g_connections[connId].socket != -1) {
                                    ssize_t bytesSent = send(g_connections[connId].socket, 
                                                         buffer + sizeof(VIRTIO_MSG_HEADER), 
                                                         length, 0);
                                    if (bytesSent <= 0) {
                                        printf("Send failed for connection %d\n", connId);
                                        CloseConnection(&g_connections[connId]);
                                    }
                                }
                            }
                        }
                    }
                }
            } else if (bytesRead < 0) {
                perror("Error reading from virtio");
                break;
            } else {
                printf("Virtio connection closed\n");
                break;
            }
        }
        
        // Check connections for data
        for (i = 0; i < MAX_CONNECTIONS; i++) {
            if (g_connections[i].inUse) {
                int pollIndex = 1;  // Start after virtio
                
                for (int j = 0; j < i; j++) {
                    if (g_connections[j].inUse) {
                        pollIndex++;
                    }
                }
                
                if (pollIndex < nfds && fds[pollIndex].revents & POLLIN) {
                    ssize_t bytesRead = recv(g_connections[i].socket, 
                                           buffer + sizeof(VIRTIO_MSG_HEADER), 
                                           BUFFER_SIZE, 0);
                    if (bytesRead > 0) {
                        // Forward data to virtio
                        if (!SendToVirtio(i, buffer + sizeof(VIRTIO_MSG_HEADER), bytesRead)) {
                            printf("Failed to send data to virtio for connection %d\n", i);
                            CloseConnection(&g_connections[i]);
                        }
                    } else {
                        // Connection closed or error
                        printf("Connection %d closed\n", i);
                        CloseConnection(&g_connections[i]);
                    }
                }
            }
        }
    }
    
    CleanupVirtio();
    return 0;
}

bool InitializeVirtio(void) {
    printf("Attempting to connect to virtio socket at: %s\n", VIRTIO_DEVICE);
    
    // Create a socket
    g_virtioFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_virtioFd < 0) {
        printf("Error creating socket: %s (errno=%d)\n", strerror(errno), errno);
        perror("Failed to create socket");
        return false;
    }
    
    // Set up the address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VIRTIO_DEVICE, sizeof(addr.sun_path) - 1);
    
    // Connect to the socket
    if (connect(g_virtioFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("Error connecting to socket: %s (errno=%d)\n", strerror(errno), errno);
        perror("Failed to connect to virtio socket");
        close(g_virtioFd);
        g_virtioFd = -1;
        return false;
    }
    
    printf("Successfully connected to virtio socket, fd=%d\n", g_virtioFd);
    
    // Wait a moment to ensure connection is fully established
    sleep(1);
    
    // Send a test message to verify the connection
    for (int i = 0; i < 5; i++) {  // Try sending 5 times with different patterns
        uint8_t testBuffer[sizeof(VIRTIO_MSG_HEADER) + 32];
        VIRTIO_MSG_HEADER* header = (VIRTIO_MSG_HEADER*)testBuffer;
        header->connId = 0xFFFF;  // Special test connection ID
        header->length = 16;      // Length of test message
        
        // Prepare a test pattern that's easy to recognize
        uint8_t* payload = testBuffer + sizeof(VIRTIO_MSG_HEADER);
        for (int j = 0; j < 16; j++) {
            payload[j] = (i * 16) + j;  // Create a recognizable pattern
        }
        
        // Print the exact bytes we're sending for debugging
        printf("Sending test message %d, %zu bytes: ", i+1, sizeof(VIRTIO_MSG_HEADER) + 16);
        for (size_t j = 0; j < sizeof(VIRTIO_MSG_HEADER) + 16; j++) {
            printf("%02X ", testBuffer[j]);
        }
        printf("\n");
        
        // Send the message
        ssize_t bytesSent = write(g_virtioFd, testBuffer, sizeof(VIRTIO_MSG_HEADER) + 16);
        if (bytesSent == sizeof(VIRTIO_MSG_HEADER) + 16) {
            printf("Test message %d sent successfully: %zd bytes\n", i+1, bytesSent);
        } else {
            if (bytesSent < 0) {
                perror("Failed to send test message");
            } else {
                printf("Incomplete test message sent: %zd/%zu bytes\n", 
                      bytesSent, sizeof(VIRTIO_MSG_HEADER) + 16);
            }
        }
        
        // Small delay between sends
        usleep(200000);  // 200ms
    }
    
    // Set non-blocking mode
    int flags = fcntl(g_virtioFd, F_GETFL, 0);
    if (flags == -1) {
        perror("Failed to get flags for virtio device");
        close(g_virtioFd);
        g_virtioFd = -1;
        return false;
    }
    
    if (fcntl(g_virtioFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set non-blocking mode for virtio device");
        close(g_virtioFd);
        g_virtioFd = -1;
        return false;
    }
    
    printf("VirtIO socket setup complete and ready for connections\n");
    return true;
}

void CleanupVirtio(void) {
    // Close all connections
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (g_connections[i].inUse) {
            CloseConnection(&g_connections[i]);
        }
    }
    
    // Close virtio device
    if (g_virtioFd != -1) {
        close(g_virtioFd);
        g_virtioFd = -1;
    }
}

bool HandleConnectionRequest(uint16_t connId, uint8_t* data, uint16_t length) {
    if (connId >= MAX_CONNECTIONS || g_connections[connId].inUse) {
        printf("Invalid connection ID in request: %d\n", connId);
        return false;
    }
    
    uint8_t atyp = data[0];
    uint16_t port;
    char host[256];
    int hostLen;
    struct addrinfo hints, *res;
    int sockfd;
    
    // Parse connection request
    switch (atyp) {
        case SOCKS_ATYP_IPV4:
            if (length < 1 + 4 + 2) {
                printf("Invalid IPv4 connection request\n");
                return false;
            }
            
            sprintf(host, "%d.%d.%d.%d", data[1], data[2], data[3], data[4]);
            port = (data[5] << 8) | data[6];
            break;
            
        case SOCKS_ATYP_DOMAIN:
            if (length < 2) {
                printf("Invalid domain connection request\n");
                return false;
            }
            
            hostLen = data[1];
            if (length < 2 + hostLen + 2) {
                printf("Invalid domain connection request (domain truncated)\n");
                return false;
            }
            
            memcpy(host, &data[2], hostLen);
            host[hostLen] = '\0';
            port = (data[2 + hostLen] << 8) | data[2 + hostLen + 1];
            break;
            
        default:
            printf("Unsupported address type: %d\n", atyp);
            return false;
    }
    
    printf("Connection request: %s:%d (ID: %d)\n", host, port, connId);
    
    // Connect to the target server
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    char portStr[8];
    sprintf(portStr, "%d", port);
    
    if (getaddrinfo(host, portStr, &hints, &res) != 0) {
        perror("getaddrinfo failed");
        return false;
    }
    
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("socket failed");
        freeaddrinfo(res);
        return false;
    }
    
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect failed");
        close(sockfd);
        freeaddrinfo(res);
        return false;
    }
    
    freeaddrinfo(res);
    
    // Set socket to non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // Store connection info
    g_connections[connId].socket = sockfd;
    g_connections[connId].inUse = true;
    g_connections[connId].connId = connId;
    
    printf("Connection %d established\n", connId);
    return true;
}

bool SendToVirtio(uint16_t connId, const uint8_t* data, uint16_t length) {
    uint8_t buffer[BUFFER_SIZE + sizeof(VIRTIO_MSG_HEADER)];
    VIRTIO_MSG_HEADER* header = (VIRTIO_MSG_HEADER*)buffer;
    ssize_t bytesSent;
    
    if (length > BUFFER_SIZE) {
        printf("Data too large for virtio buffer\n");
        return false;
    }
    
    // Prepare header
    header->connId = connId;
    header->length = length;
    
    // Copy data
    memcpy(buffer + sizeof(VIRTIO_MSG_HEADER), data, length);
    
    // Send to virtio
    bytesSent = write(g_virtioFd, buffer, sizeof(VIRTIO_MSG_HEADER) + length);
    if (bytesSent != (ssize_t)(sizeof(VIRTIO_MSG_HEADER) + length)) {
        if (bytesSent < 0) {
            perror("write to virtio failed");
        } else {
            printf("Incomplete write to virtio: %zd/%zu bytes\n", 
                  bytesSent, sizeof(VIRTIO_MSG_HEADER) + length);
        }
        return false;
    }
    
    return true;
}

void CloseConnection(CONNECTION_INFO* conn) {
    if (!conn->inUse) {
        return;
    }
    
    if (conn->socket != -1) {
        close(conn->socket);
        conn->socket = -1;
    }
    
    conn->inUse = false;
    printf("Connection %d closed\n", conn->connId);
} 