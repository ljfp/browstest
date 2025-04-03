# Makefile for Async SOCKS Server with VirtIO Multiplexing

# Windows SOCKS server (using Microsoft Visual C++ compiler)
WIN_CC := cl
WIN_CFLAGS := /W4 /MT /EHsc
WIN_LDFLAGS := /link ws2_32.lib
WIN_SRCS := main.c
WIN_TARGET := socks_server.exe

# Linux host proxy
LINUX_CC := gcc
LINUX_CFLAGS := -Wall -Wextra -O2
LINUX_LDFLAGS :=
LINUX_SRCS := host_proxy.c
LINUX_TARGET := host_proxy

# Default target does nothing as we need different environments
all:
	@echo "Please use 'make windows' for Windows SOCKS server"
	@echo "or 'make linux' for Linux host proxy"

# Windows target
windows:
	$(WIN_CC) $(WIN_CFLAGS) $(WIN_SRCS) $(WIN_LDFLAGS) /Fe:$(WIN_TARGET)

# Linux target
linux:
	$(LINUX_CC) $(LINUX_CFLAGS) $(LINUX_SRCS) -o $(LINUX_TARGET) $(LINUX_LDFLAGS)

# Clean target
clean:
	$(RM) $(WIN_TARGET) *.obj *.pdb *.ilk
	$(RM) $(LINUX_TARGET)

.PHONY: all windows linux clean 