#!/bin/bash
echo "Building host proxy for Linux..."

# Check if gcc is installed
if ! command -v gcc &> /dev/null; then
    echo "Error: gcc compiler not found. Please install gcc."
    exit 1
fi

# Compile the host proxy
gcc -Wall -Wextra -O2 host_proxy.c -o host_proxy

# Check if compilation was successful
if [ $? -ne 0 ]; then
    echo "Build failed."
    exit 1
fi

# Set executable permissions
chmod +x host_proxy
chmod +x test_proxy.sh

echo ""
echo "Build successful! The host proxy is available as host_proxy"
echo ""
echo "To run the host proxy: sudo ./host_proxy"
echo "Note: sudo might be required to access the virtio socket"
exit 0 