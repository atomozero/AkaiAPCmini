# Troubleshooting Guide - APC Mini Test Application

This guide provides solutions for common issues encountered when using the APC Mini Test Application on Haiku OS.

## Quick Diagnostic Commands

Before diving into specific issues, run these commands to gather system information:

```bash
# System information
uname -a
listdev | grep -i usb
listdev | grep -i midi

# Application diagnostics
cd apc_mini_project/build
make detect-usb
make detect-midi
./apc_mini_test_debug --version --verbose
```

## Common Issues and Solutions

### 1. Device Detection Issues

#### Problem: APC Mini Not Detected
```
Error: APC_ERROR_DEVICE_NOT_FOUND
APC Mini not found via USB scan
```

**Diagnostic Steps:**
```bash
# Check USB subsystem
listdev | grep -i usb
ls -la /dev/bus/usb/

# Check if device is powered and connected
lsusb  # If available
```

**Solutions:**
1. **Hardware Check:**
   - Verify USB cable connection
   - Try different USB port
   - Check APC Mini power LED
   - Test with different USB cable

2. **USB Permissions:**
   ```bash
   # Check USB device permissions
   ls -la /dev/bus/usb/raw*

   # If permission denied, may need to run as root (not recommended)
   sudo ./apc_mini_test_debug
   ```

3. **USB Raw Support:**
   ```bash
   # Check if USB raw support is available
   ls /dev/bus/usb/

   # If directory missing, USB raw may not be compiled in kernel
   # Use MIDI fallback mode
   ./apc_mini_test_debug --force-midi
   ```

#### Problem: Wrong Device Detected
```
Device VID:PID 1234:5678 doesn't match APC Mini 09e8:0028
```

**Solution:**
```bash
# List all USB devices to find correct APC Mini
make detect-usb

# Look for:
# VID:PID = 09e8:0028 (standard APC Mini)
# Product name containing "APC" or "MINI"
```

### 2. USB Raw Access Issues

#### Problem: USB Raw Open Failed
```
Error: APC_ERROR_USB_OPEN_FAILED
Failed to open device /dev/bus/usb/raw0: Permission denied
```

**Solutions:**
1. **Permission Fix:**
   ```bash
   # Check current permissions
   ls -la /dev/bus/usb/raw*

   # Temporary fix (per session)
   chmod 666 /dev/bus/usb/raw*

   # Permanent fix: Create udev-like rule (Haiku-specific)
   # This may require kernel-level changes
   ```

2. **Alternative Access:**
   ```bash
   # Use MIDI fallback instead
   ./apc_mini_test_debug --no-usb-raw
   ```

#### Problem: USB Transfer Failed
```
Error: APC_ERROR_USB_TRANSFER_FAILED
USB bulk transfer failed: Input/output error
```

**Diagnostic Steps:**
```bash
# Check USB subsystem health
dmesg | grep -i usb | tail -20

# Test with different transfer sizes
./apc_mini_test_debug --usb-packet-size 4
```

**Solutions:**
1. **USB Cable/Hub Issues:**
   - Try direct connection (bypass USB hubs)
   - Use high-quality USB cable
   - Try different USB port

2. **Power Issues:**
   - Ensure stable power supply
   - Avoid USB bus overload
   - Try powered USB hub

3. **Driver Issues:**
   ```bash
   # Restart USB subsystem (if possible)
   # May require system restart
   shutdown -r now
   ```

### 3. MIDI System Issues

#### Problem: MIDI Initialization Failed
```
Error: APC_ERROR_MIDI_INIT_FAILED
Failed to register MIDI consumer
```

**Diagnostic Steps:**
```bash
# Check MIDI services
ps aux | grep midi

# Check MIDI device listing
listdev | grep -i midi
```

**Solutions:**
1. **MIDI Server Issues:**
   ```bash
   # Restart MIDI server
   killall midi_server
   /system/servers/midi_server &

   # Verify restart
   ps aux | grep midi_server
   ```

2. **MIDI Roster Issues:**
   ```bash
   # Check MIDI roster availability
   listdev | grep -i midi

   # If no MIDI devices shown, MIDI subsystem may be broken
   # Try system restart
   ```

3. **Application Permissions:**
   ```bash
   # Ensure application has MIDI access permissions
   # May need to run from Terminal rather than GUI
   ```

#### Problem: No MIDI Devices Found
```
Available MIDI devices:
(no devices listed)
```

**Solutions:**
1. **Service Restart:**
   ```bash
   # Full MIDI system restart
   killall midi_server
   sleep 2
   /system/servers/midi_server &
   sleep 2
   ./apc_mini_test_debug
   ```

2. **USB MIDI Driver:**
   ```bash
   # Check if USB MIDI driver is loaded
   listdev | grep -i usb

   # APC Mini should appear as both USB and MIDI device
   ```

### 4. Performance Issues

#### Problem: High Latency
```
Latency - Min: 50000 μs, Max: 100000 μs, Avg: 75000 μs
(Expected: <10000 μs)
```

**Solutions:**
1. **USB Performance:**
   ```bash
   # Use USB Raw mode (lower latency)
   ./apc_mini_test_debug --force-usb-raw

   # Reduce USB transfer timeout
   ./apc_mini_test_debug --usb-timeout 10
   ```

2. **System Load:**
   ```bash
   # Check system load
   top

   # Close unnecessary applications
   # Increase application priority
   chrt -f 50 ./apc_mini_test_debug
   ```

3. **Buffer Settings:**
   ```bash
   # Reduce buffer sizes in USB raw mode
   # Edit usb_raw_midi.cpp, reduce transfer sizes
   ```

#### Problem: Message Loss
```
Stress test results: Expected 2000 messages, received 1800
```

**Solutions:**
1. **Increase Buffer Sizes:**
   - Edit `USB_TRANSFER_TIMEOUT_MS` in apc_mini_defs.h
   - Increase to 200ms or higher

2. **Reduce Message Rate:**
   ```bash
   # Run stress test with lower message count
   ./apc_mini_test_debug --stress-messages 500
   ```

### 5. Build and Compilation Issues

#### Problem: Missing Headers
```
error: Be/Application.h: No such file or directory
```

**Solution:**
```bash
# Install development packages
pkgman install haiku_devel midi_devel

# Verify installation
ls /boot/system/develop/headers/be/
ls /boot/system/develop/headers/be/midi/
```

#### Problem: Linker Errors
```
undefined reference to `BMidiRoster::MidiRoster()'
```

**Solution:**
```bash
# Check library linking in Makefile
grep "HAIKU_LIBS" build/Makefile

# Should include: -lbe -lmidi -lroot -ldevice
# If missing, add to HAIKU_LIBS in Makefile
```

#### Problem: C++17 Features Not Supported
```
error: 'std::function' was not declared in this scope
```

**Solution:**
```bash
# Check GCC version
gcc --version

# Should be 8.3.0 or later for C++17
# Update if necessary:
pkgman update gcc

# Or modify Makefile to use C++14:
# Change -std=c++17 to -std=c++14
```

### 6. Runtime Crashes

#### Problem: Segmentation Fault on Startup
```
Segmentation violation (core dumped)
```

**Diagnostic Steps:**
```bash
# Run with debugger
gdb ./apc_mini_test_debug
(gdb) run
(gdb) bt  # When crash occurs

# Check for common issues:
# - Null pointer dereference
# - Uninitialized memory
# - Stack overflow
```

**Solutions:**
1. **Debug Build:**
   ```bash
   # Use debug build with symbols
   make debug
   ./apc_mini_test_debug
   ```

2. **Memory Issues:**
   ```bash
   # Run with valgrind (if available)
   valgrind ./apc_mini_test_debug --simulation
   ```

#### Problem: Application Hangs
```
Application starts but becomes unresponsive
```

**Solutions:**
1. **Thread Issues:**
   ```bash
   # Check for deadlocks in thread synchronization
   # Kill and restart application
   killall apc_mini_test_debug
   ```

2. **USB Blocking:**
   ```bash
   # USB operations may be blocking
   # Try simulation mode first
   ./apc_mini_test_debug --simulation
   ```

### 7. USB Passthrough Issues (QEMU)

#### Problem: APC Mini Not Available in VM
```
No USB devices detected in Haiku VM
```

**Host System Checks:**
```bash
# Verify device on host
lsusb | grep 09e8

# Check QEMU USB passthrough
# In QEMU monitor:
info usb
```

**Solutions:**
1. **QEMU Configuration:**
   ```bash
   # Correct QEMU USB passthrough syntax:
   -usb -device usb-host,vendorid=0x09e8,productid=0x0028

   # Or by bus/device:
   -device usb-host,hostbus=X,hostaddr=Y
   ```

2. **Host Permissions:**
   ```bash
   # Create udev rule on host (Linux):
   # /etc/udev/rules.d/99-apc-mini.rules
   SUBSYSTEM=="usb", ATTRS{idVendor}=="09e8", ATTRS{idProduct}=="0028", MODE="0666"

   # Reload rules:
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

## Advanced Debugging

### Enable Verbose Logging
```bash
# Build with debug flags
make debug

# Run with maximum verbosity
./apc_mini_test_debug --verbose --debug-usb --debug-midi
```

### USB Raw Debug
```cpp
// In usb_raw_midi.cpp, add debug prints:
printf("USB transfer: endpoint=0x%02x, length=%zu\n",
       transfer.endpoint, transfer.length);
```

### MIDI Debug
```cpp
// In apc_mini_test.cpp, add MIDI debugging:
printf("MIDI: status=0x%02x, data1=0x%02x, data2=0x%02x\n",
       status, data1, data2);
```

### Memory Debug
```bash
# Enable address sanitizer (if supported)
# Add to Makefile CXXFLAGS:
CXXFLAGS += -fsanitize=address -g

# Rebuild and test
make clean debug
./apc_mini_test_debug --simulation
```

## System-Specific Issues

### Haiku R1/beta5 Issues
- **USB Stack**: May have incomplete USB MIDI support
- **MIDI Server**: Occasional crashes, restart required
- **Threading**: Be careful with thread priorities

### QEMU VM Issues
- **USB Timing**: May have different timing characteristics
- **Performance**: Lower performance than native hardware
- **Passthrough**: USB passthrough may not work with all host configurations

## Getting Help

### Information to Collect
Before reporting issues, collect:
```bash
# System information
uname -a
listdev > system_devices.txt

# Application debug output
./apc_mini_test_debug --verbose > debug_output.txt 2>&1

# USB information
make detect-usb > usb_info.txt

# Build information
make --version
gcc --version
```

### Contact Information
1. **GitHub Issues**: Project repository issues page
2. **Haiku Forums**: Haiku OS community forums
3. **IRC**: #haiku on Libera.Chat

### Bug Report Template
```
**Environment:**
- Haiku Version: (output of 'uname -a')
- Hardware: (APC Mini model, USB controller)
- VM/Native: (QEMU version if applicable)

**Issue:**
- Expected behavior:
- Actual behavior:
- Error messages:

**Reproduction:**
1. Step 1
2. Step 2
3. Error occurs

**Debug Output:**
(paste relevant debug output)

**Additional Information:**
- Workarounds attempted:
- Other MIDI devices working:
```

---

**Troubleshooting Guide v1.0.0**
*APC Mini Test Application for Haiku OS*