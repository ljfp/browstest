#!/bin/bash
echo "Testing host proxy..."

# Check if the host proxy executable exists
if [ ! -f "./host_proxy" ]; then
    echo "Error: host_proxy not found."
    echo "Please build the host proxy first using build_linux.sh"
    exit 1
fi

# Check if virtio socket exists
if [ ! -e "/tmp/vserial" ]; then
    echo "Error: The virtio socket (/tmp/vserial) was not found."
    echo "Make sure your Windows VM is running with the virtio-serial device properly configured."
    exit 1
fi

# Check socket permissions
if [ ! -w "/tmp/vserial" ]; then
    echo "Warning: You may not have write permission on the virtio socket."
    echo "The host proxy may need to be run with sudo."
fi

echo ""
echo "Starting host proxy (use Ctrl+C to stop)..."
echo "Note: This may require sudo if socket permissions are restricted."
echo ""

# Start the host proxy
sudo ./host_proxy

exit 0 