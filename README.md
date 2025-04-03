# Async SOCKS Server with VirtIO Multiplexing

This project implements an asynchronous SOCKS5 proxy server for Windows that multiplexes connections over a virtio-serial port to a host program. The server uses Windows IOCP (I/O Completion Ports) for high-performance asynchronous I/O and works without dynamic memory allocation.

## Components

1. **SOCKS Server (Windows Guest)**
   - Listens for SOCKS5 connections on port 1080 (configurable)
   - Uses IOCP for asynchronous I/O
   - Multiplexes connections over virtio-serial
   - No dynamic memory allocation (fixed connection pool)

2. **Host Proxy (Linux Host)**
   - Connects to the virtio-serial device
   - Demultiplexes connections from the guest
   - Establishes connections to target servers
   - Routes data between guest and target servers

## Building

### Windows SOCKS Server

Compile the SOCKS server on Windows:

```
cl /W4 /MT /EHsc main.c /link ws2_32.lib
```

### Linux Host Proxy

Compile the host proxy on Linux:

```
gcc -Wall -Wextra -o host_proxy host_proxy.c
```

## Setup

### VirtIO Configuration

1. Configure your VM to have a virtio-serial device
   - In QEMU, add something like: `-device virtio-serial -chardev socket,path=/tmp/vserial,server=on,wait=off,id=vserial0 -device virtserialport,chardev=vserial0,name=com.redhat.spice.0`
   - In other virtualization platforms, follow their specific instructions for virtio-serial setup

2. On Windows (guest), the virtio-serial device typically appears as a COM port
   - Update the `VIRTIO_DEVICE` macro in `socks_server.h` to match your COM port (default: `\\\\.\\COM1`)

3. On Linux (host), the virtio-serial device typically appears as `/dev/virtio-ports/com.redhat.spice.0`
   - Update the `VIRTIO_DEVICE` macro in `host_proxy.c` if needed

## Usage

1. Start the host proxy on the Linux host:
   ```
   sudo ./host_proxy
   ```

2. Start the SOCKS server on the Windows guest:
   ```
   socks_server.exe
   ```

3. Configure your applications to use the SOCKS5 proxy at `127.0.0.1:1080`

## Features

- Supports SOCKS5 protocol (RFC 1928)
- Handles both IPv4 and domain name resolution
- Supports multiple simultaneous connections (default: 64, configurable)
- Fast, asynchronous I/O with Windows IOCP
- Fixed memory footprint (no dynamic allocation)
- Simple protocol for virtio-serial multiplexing

## Limitations

- Only supports SOCKS5 CONNECT command (no BIND or UDP ASSOCIATE)
- No authentication mechanism (only SOCKS5 NO_AUTH)
- IPv6 addressing not implemented (can be added easily)
- Fixed buffer sizes (4KB per connection by default)

## Protocol

The protocol for multiplexing over virtio-serial is simple:

```
struct {
    uint16_t connId;   // Connection ID (0-63)
    uint16_t length;   // Length of data following this header
    uint8_t data[];    // Variable-length data payload
}
```

When a new connection is established, the first packet contains the SOCKS connection request information (address type, address, port). Subsequent packets for that connection ID contain raw data to be sent to the target server.

## License

This project is placed in the public domain. 