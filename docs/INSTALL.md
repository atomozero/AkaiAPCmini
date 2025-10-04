# Installation Guide - APC Mini Test Application

This guide covers installation and setup of the APC Mini Test Application on Haiku OS, including QEMU development environment setup.

## Prerequisites

### Haiku OS Requirements
- **Haiku Version**: R1/beta5 or later (recommended: latest nightly)
- **Architecture**: x86_64 (primary support), x86_gcc2 (legacy)
- **RAM**: Minimum 2GB, recommended 4GB for development
- **Storage**: At least 500MB free space
- **USB**: Working USB subsystem for APC Mini connection

### Development Environment (Optional)
- **Host OS**: Linux, macOS, or Windows with WSL2
- **QEMU**: Version 5.0+ for Haiku virtualization
- **Network**: Internet connection for package downloads

## Installation Methods

### Method 1: Pre-built Package (Recommended)

#### Step 1: Download Package
```bash
# On Haiku system, download from release
wget https://github.com/your-repo/apc-mini-test/releases/latest/apc_mini_test-1.0.0-x86_64.hpkg

# Or transfer from host system via web server
```

#### Step 2: Install Package
```bash
# Install using Haiku package manager
pkgman install apc_mini_test-1.0.0-x86_64.hpkg

# Application will be available in Applications menu
```

### Method 2: Build from Source

#### Step 1: Install Development Tools
```bash
# Install essential development packages
pkgman install gcc developmentbase

# Install Be/MIDI development headers
pkgman install haiku_devel midi_devel

# Optional: Install debugging tools
pkgman install gdb binutils
```

#### Step 2: Get Source Code
```bash
# Create development directory
mkdir -p /boot/home/develop
cd /boot/home/develop

# Clone from repository
git clone https://github.com/atomozero/AkaiAPCmini.git
cd AkaiAPCmini
```

#### Step 3: Build Application
```bash
cd build

# Build debug version (recommended for first build)
make debug

# Test build
./apc_mini_test_debug --simulation

# If successful, build release version
make release
```

#### Step 4: Install Locally
```bash
# Install to user desktop
make install

# Or create system package
make package
pkgman install apc_mini_test-1.0.0-x86_64.hpkg
```

## QEMU Development Setup

For developers using a QEMU-based Haiku development environment:

### Step 1: QEMU VM Preparation
```bash
# Download Haiku VM image
wget https://download.haiku-os.org/r1beta5/release/x86_64/haiku-r1beta5-x86_64-anyboot.iso

# Create VM disk
qemu-img create -f qcow2 haiku-dev.qcow2 20G

# Install Haiku (one-time setup)
qemu-system-x86_64 \
    -m 4G \
    -smp 4 \
    -enable-kvm \
    -drive file=haiku-dev.qcow2,format=qcow2 \
    -cdrom haiku-r1beta5-x86_64-anyboot.iso \
    -boot d \
    -usb \
    -device usb-host,vendorid=0x09e8,productid=0x0028  # APC Mini passthrough
```

### Step 2: USB Device Passthrough
```bash
# For APC Mini USB passthrough in QEMU
# First, find the device on host
lsusb | grep 09e8

# Add to QEMU command line
-device usb-host,vendorid=0x09e8,productid=0x0028

# Or use host bus/device numbers
-device usb-host,hostbus=1,hostaddr=5
```

### Step 3: File Transfer Setup
```bash
# Method 1: HTTP server on host (port 8080)
cd AkaiAPCmini
python3 -m http.server 8080

# In Haiku VM:
wget http://10.0.2.2:8080/apc_mini_project.zip
```

```bash
# Method 2: Shared folder (if supported)
qemu-system-x86_64 \
    ... \
    -virtfs local,path=/host/path/to/project,mount_tag=project,security_model=passthrough

# In Haiku VM:
mkdir /mnt/shared
mount -t 9p -o trans=virtio project /mnt/shared
```

### Step 4: Development Workflow
```bash
# Start QEMU with development setup
qemu-system-x86_64 \
    -m 4G \
    -smp 4 \
    -enable-kvm \
    -drive file=haiku-dev.qcow2,format=qcow2 \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device e1000,netdev=net0 \
    -usb \
    -device usb-host,vendorid=0x09e8,productid=0x0028 \
    -monitor stdio
```

## Configuration

### Step 1: USB Permissions
```bash
# Check USB raw device access
ls -la /dev/bus/usb/

# If devices not accessible, check permissions
ls -la /dev/bus/usb/raw*

# May need to add user to appropriate group
# (Haiku-specific user management)
```

### Step 2: MIDI System Verification
```bash
# Check MIDI services
ps aux | grep midi

# Restart MIDI services if needed
/system/servers/midi_server &

# Test MIDI device detection
midiscan  # if available
```

### Step 3: Application Configuration
```bash
# Create configuration directory
mkdir -p /boot/home/config/settings/apc_mini_test

# Run initial setup
apc_mini_test --setup

# Verify device detection
apc_mini_test --detect
```

## Verification

### Step 1: Hardware Detection
```bash
# Check USB device presence
make detect-usb

# Should show APC Mini with VID:PID 09e8:0028
# Example output:
# Device: /dev/bus/usb/raw0
#   VID:PID = 09e8:0028
#   Product: APC MINI
#   *** This is an APC Mini! ***
```

### Step 2: MIDI Verification
```bash
# Check MIDI device listing
make detect-midi

# Should show APC Mini in MIDI device list
```

### Step 3: Application Test
```bash
# Run simulation mode (no hardware required)
apc_mini_test --simulation

# Expected output:
# APC Mini Test Application for Haiku OS
# =====================================
# Falling back to Haiku MIDI
# Starting interactive test mode...
```

### Step 4: Hardware Test
```bash
# Connect APC Mini via USB
# Run interactive mode
apc_mini_test

# Expected output:
# Using USB Raw access mode
# Starting interactive test mode...
# Press keys for commands (h for help, q to quit):
```

## Troubleshooting Installation

### Build Errors

#### Missing Development Headers
```bash
# Error: Be/Application.h not found
# Solution:
pkgman install haiku_devel

# Error: MidiRoster.h not found
# Solution:
pkgman install midi_devel
```

#### Compiler Issues
```bash
# Error: C++17 features not supported
# Solution: Verify GCC version
gcc --version  # Should be 8.3.0 or later

# Update if necessary
pkgman update gcc
```

### Runtime Errors

#### USB Raw Access Failed
```bash
# Error: Cannot open /dev/bus/usb/raw
# Check device files:
ls -la /dev/bus/usb/

# If missing, USB raw support may not be compiled into kernel
# Use MIDI fallback mode
```

#### Permission Denied
```bash
# Error: Permission denied accessing USB device
# Solution: Check user permissions
whoami
groups

# May need to run as root for USB raw access (not recommended)
# Better: Fix USB device permissions
```

#### MIDI Service Issues
```bash
# Error: MIDI initialization failed
# Check MIDI server status:
ps aux | grep midi

# Restart MIDI services:
killall midi_server
/system/servers/midi_server &
```

### USB Passthrough Issues (QEMU)

#### Device Not Detected in VM
```bash
# Check USB device on host:
lsusb | grep 09e8

# Verify QEMU USB passthrough:
# In QEMU monitor:
info usb

# Should show APC Mini device
```

#### Permission Issues on Host
```bash
# Host may need udev rules for USB access
# Create /etc/udev/rules.d/99-apc-mini.rules:
SUBSYSTEM=="usb", ATTRS{idVendor}=="09e8", ATTRS{idProduct}=="0028", MODE="0666"

# Reload udev rules:
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Performance Optimization

### Build Optimizations
```bash
# Use release build for production
make release

# Enable specific optimizations in Makefile:
CXXFLAGS += -O3 -march=native -flto
```

### Runtime Optimizations
```bash
# Increase thread priority (if needed)
chrt -f 50 apc_mini_test

# Monitor performance
apc_mini_test --latency-test
```

## Uninstallation

### Package-based Installation
```bash
# Remove package
pkgman uninstall apc_mini_test

# Remove configuration
rm -rf /boot/home/config/settings/apc_mini_test
```

### Source-based Installation
```bash
# Remove installed files
rm -rf /boot/home/Desktop/APC_Mini_Test

# Remove build artifacts
cd build
make distclean
```

## Next Steps

After successful installation:

1. **Read Documentation**: Review README.md for usage instructions
2. **Run Tests**: Start with simulation mode, then hardware tests
3. **Explore Examples**: Try LED patterns and MIDI monitor utilities
4. **Development**: Set up Genio IDE project for modifications

## Support

For installation issues:
1. Check Haiku system requirements and compatibility
2. Verify USB and MIDI subsystem functionality
3. Review troubleshooting section
4. Report issues with full system information

---

**Installation Guide v1.0.0**
*APC Mini Test Application for Haiku OS*