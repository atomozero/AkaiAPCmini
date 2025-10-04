#!/bin/bash

# QEMU Setup Script for APC Mini Test Development on Haiku OS
# This script sets up a QEMU virtual machine for Haiku development
# with USB passthrough for APC Mini testing

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VM_NAME="haiku-apc-mini-dev"
VM_DIR="$HOME/VMs/$VM_NAME"
HAIKU_VERSION="r1beta5"
MEMORY="4G"
CPUS="4"
DISK_SIZE="20G"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check QEMU
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        log_error "QEMU not found. Please install QEMU first."
        log_info "Ubuntu/Debian: sudo apt install qemu-system-x86"
        log_info "Fedora: sudo dnf install qemu-system-x86"
        log_info "macOS: brew install qemu"
        exit 1
    fi

    # Check wget or curl
    if ! command -v wget &> /dev/null && ! command -v curl &> /dev/null; then
        log_error "Neither wget nor curl found. Please install one of them."
        exit 1
    fi

    # Check disk space (at least 5GB)
    available_space=$(df -BG "$HOME" | awk 'NR==2 {print $4}' | sed 's/G//')
    if [ "$available_space" -lt 5 ]; then
        log_error "Insufficient disk space. At least 5GB required."
        exit 1
    fi

    log_success "Prerequisites check passed"
}

# Create VM directory structure
setup_vm_directory() {
    log_info "Setting up VM directory: $VM_DIR"

    mkdir -p "$VM_DIR"
    mkdir -p "$VM_DIR/iso"
    mkdir -p "$VM_DIR/scripts"
    mkdir -p "$VM_DIR/shared"

    log_success "VM directory created"
}

# Download Haiku ISO
download_haiku() {
    local iso_file="$VM_DIR/iso/haiku-${HAIKU_VERSION}-x86_64-anyboot.iso"

    if [ -f "$iso_file" ]; then
        log_info "Haiku ISO already exists: $iso_file"
        return
    fi

    log_info "Downloading Haiku $HAIKU_VERSION ISO..."

    local download_url="https://download.haiku-os.org/${HAIKU_VERSION}/release/x86_64/haiku-${HAIKU_VERSION}-x86_64-anyboot.iso"

    if command -v wget &> /dev/null; then
        wget -O "$iso_file" "$download_url"
    else
        curl -L -o "$iso_file" "$download_url"
    fi

    if [ ! -f "$iso_file" ]; then
        log_error "Failed to download Haiku ISO"
        exit 1
    fi

    log_success "Haiku ISO downloaded: $iso_file"
}

# Create VM disk
create_vm_disk() {
    local disk_file="$VM_DIR/${VM_NAME}.qcow2"

    if [ -f "$disk_file" ]; then
        log_info "VM disk already exists: $disk_file"
        return
    fi

    log_info "Creating VM disk: $disk_file ($DISK_SIZE)"

    qemu-img create -f qcow2 "$disk_file" "$DISK_SIZE"

    if [ ! -f "$disk_file" ]; then
        log_error "Failed to create VM disk"
        exit 1
    fi

    log_success "VM disk created: $disk_file"
}

# Find APC Mini USB device
find_apc_mini() {
    log_info "Searching for APC Mini USB device..."

    # Try lsusb first
    if command -v lsusb &> /dev/null; then
        local apc_device=$(lsusb | grep "09e8:0028" || true)
        if [ -n "$apc_device" ]; then
            log_success "Found APC Mini: $apc_device"

            # Extract bus and device numbers
            local bus=$(echo "$apc_device" | awk '{print $2}')
            local device=$(echo "$apc_device" | awk '{print $4}' | sed 's/://')

            echo "USB_PASSTHROUGH_BUS=$bus"
            echo "USB_PASSTHROUGH_DEVICE=$device"
            return
        fi
    fi

    # Fallback: scan /sys/bus/usb/devices
    local usb_devices=$(find /sys/bus/usb/devices -name "idVendor" -exec grep -l "09e8" {} \; 2>/dev/null || true)

    for vendor_file in $usb_devices; do
        local device_dir=$(dirname "$vendor_file")
        local product_id=$(cat "$device_dir/idProduct" 2>/dev/null || echo "")

        if [ "$product_id" = "0028" ]; then
            local usb_path=$(basename "$device_dir")
            log_success "Found APC Mini at: $usb_path"
            echo "USB_PASSTHROUGH_PATH=$usb_path"
            return
        fi
    done

    log_warning "APC Mini not found. USB passthrough will be disabled."
    log_info "Connect APC Mini and run the script again to enable USB passthrough."
    echo "USB_PASSTHROUGH_FOUND=false"
}

# Generate QEMU startup script
generate_startup_script() {
    local startup_script="$VM_DIR/scripts/start_vm.sh"
    local disk_file="$VM_DIR/${VM_NAME}.qcow2"
    local iso_file="$VM_DIR/iso/haiku-${HAIKU_VERSION}-x86_64-anyboot.iso"

    log_info "Generating startup script: $startup_script"

    # Get USB device info
    local usb_info=$(find_apc_mini)

    cat > "$startup_script" << 'EOF'
#!/bin/bash

# QEMU Haiku VM Startup Script for APC Mini Development
# Generated by setup_qemu.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VM_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_DIR="$(dirname "$(dirname "$VM_DIR")")"

EOF

    # Add configuration variables
    cat >> "$startup_script" << EOF
VM_NAME="$VM_NAME"
MEMORY="$MEMORY"
CPUS="$CPUS"
DISK_FILE="$disk_file"
ISO_FILE="$iso_file"
SHARED_DIR="$PROJECT_DIR"

EOF

    # Add USB device info if found
    echo "$usb_info" >> "$startup_script"

    # Add the main script
    cat >> "$startup_script" << 'EOF'

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Check if installation is needed
check_installation() {
    if [ ! -f "$DISK_FILE" ]; then
        log_info "VM disk not found. Will boot from ISO for installation."
        return 1
    fi

    # Check if disk has been initialized (simple heuristic)
    local disk_size=$(qemu-img info "$DISK_FILE" | grep "virtual size" | awk '{print $3}')
    if [ "$disk_size" = "0" ]; then
        log_info "VM disk appears empty. Will boot from ISO for installation."
        return 1
    fi

    return 0
}

# Build QEMU command
build_qemu_command() {
    local qemu_cmd="qemu-system-x86_64"

    # Basic VM configuration
    qemu_cmd="$qemu_cmd -m $MEMORY"
    qemu_cmd="$qemu_cmd -smp $CPUS"
    qemu_cmd="$qemu_cmd -enable-kvm"  # Enable if available

    # Disk
    qemu_cmd="$qemu_cmd -drive file=$DISK_FILE,format=qcow2"

    # Boot configuration
    if ! check_installation; then
        log_info "Adding ISO for installation"
        qemu_cmd="$qemu_cmd -cdrom $ISO_FILE"
        qemu_cmd="$qemu_cmd -boot d"
    else
        log_info "Booting from installed system"
        qemu_cmd="$qemu_cmd -boot c"
    fi

    # Network with port forwarding for SSH/HTTP
    qemu_cmd="$qemu_cmd -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080"
    qemu_cmd="$qemu_cmd -device e1000,netdev=net0"

    # USB controller
    qemu_cmd="$qemu_cmd -usb"
    qemu_cmd="$qemu_cmd -device usb-ehci,id=ehci"

    # APC Mini USB passthrough (if available)
    if [ "${USB_PASSTHROUGH_FOUND:-true}" != "false" ]; then
        if [ -n "${USB_PASSTHROUGH_BUS:-}" ] && [ -n "${USB_PASSTHROUGH_DEVICE:-}" ]; then
            log_info "Adding APC Mini USB passthrough (bus $USB_PASSTHROUGH_BUS, device $USB_PASSTHROUGH_DEVICE)"
            qemu_cmd="$qemu_cmd -device usb-host,hostbus=$USB_PASSTHROUGH_BUS,hostaddr=$USB_PASSTHROUGH_DEVICE"
        else
            log_info "Adding APC Mini USB passthrough (by VID:PID)"
            qemu_cmd="$qemu_cmd -device usb-host,vendorid=0x09e8,productid=0x0028"
        fi
    else
        log_warning "APC Mini not found - no USB passthrough"
    fi

    # Graphics and input
    qemu_cmd="$qemu_cmd -vga std"
    qemu_cmd="$qemu_cmd -device usb-tablet"  # Better mouse integration

    # Audio (optional)
    qemu_cmd="$qemu_cmd -device ac97"

    # Monitor for debugging
    qemu_cmd="$qemu_cmd -monitor stdio"

    # Serial console (useful for debugging)
    qemu_cmd="$qemu_cmd -serial file:$VM_DIR/serial.log"

    echo "$qemu_cmd"
}

# Start VM with development features
start_development_vm() {
    log_info "Starting Haiku development VM..."
    log_info "VM Name: $VM_NAME"
    log_info "Memory: $MEMORY"
    log_info "CPUs: $CPUS"
    log_info "Disk: $DISK_FILE"

    if [ ! -f "$DISK_FILE" ]; then
        log_warning "VM disk not found. Creating new disk..."
        qemu-img create -f qcow2 "$DISK_FILE" 20G
    fi

    # Build and execute QEMU command
    local qemu_cmd=$(build_qemu_command)

    log_info "QEMU Command:"
    echo "$qemu_cmd"
    echo

    log_info "Starting VM..."
    log_info "QEMU Monitor Commands:"
    log_info "  info usb          - List USB devices"
    log_info "  info network      - Show network status"
    log_info "  system_powerdown  - Graceful shutdown"
    log_info "  quit              - Force quit"
    echo

    # Start the VM
    eval "$qemu_cmd"
}

# Main execution
case "${1:-start}" in
    "start")
        start_development_vm
        ;;
    "install")
        log_info "Starting installation mode..."
        # Force installation mode by temporarily moving disk
        if [ -f "$DISK_FILE" ]; then
            mv "$DISK_FILE" "$DISK_FILE.backup"
        fi
        qemu-img create -f qcow2 "$DISK_FILE" 20G
        start_development_vm
        ;;
    "console")
        log_info "Starting console-only mode..."
        qemu_cmd=$(build_qemu_command)
        qemu_cmd="$qemu_cmd -nographic"
        eval "$qemu_cmd"
        ;;
    "help")
        echo "Usage: $0 [start|install|console|help]"
        echo "  start   - Start VM (default)"
        echo "  install - Force installation mode"
        echo "  console - Start in console mode"
        echo "  help    - Show this help"
        ;;
    *)
        log_error "Unknown command: $1"
        echo "Use '$0 help' for usage information"
        exit 1
        ;;
esac
EOF

    chmod +x "$startup_script"
    log_success "Startup script created: $startup_script"
}

# Generate development helper scripts
generate_helper_scripts() {
    local scripts_dir="$VM_DIR/scripts"

    # File transfer script
    log_info "Creating file transfer script..."
    cat > "$scripts_dir/transfer_files.sh" << 'EOF'
#!/bin/bash

# File Transfer Script for Haiku VM Development
# Transfers project files to running Haiku VM

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VM_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_DIR="$(dirname "$(dirname "$VM_DIR")")"

log_info() {
    echo -e "\033[0;34m[INFO]\033[0m $1"
}

log_success() {
    echo -e "\033[0;32m[SUCCESS]\033[0m $1"
}

log_error() {
    echo -e "\033[0;31m[ERROR]\033[0m $1"
}

# Start HTTP server for file transfer
start_http_server() {
    local port=${1:-8080}

    log_info "Starting HTTP server on port $port..."
    log_info "Project directory: $PROJECT_DIR"

    cd "$PROJECT_DIR"

    if command -v python3 &> /dev/null; then
        python3 -m http.server $port
    elif command -v python &> /dev/null; then
        python -m SimpleHTTPServer $port
    else
        log_error "Python not found. Cannot start HTTP server."
        exit 1
    fi
}

# Generate download commands for Haiku
generate_download_commands() {
    local host_ip=${1:-10.0.2.2}
    local port=${2:-8080}

    echo "# Commands to run in Haiku VM:"
    echo "mkdir -p /boot/home/develop"
    echo "cd /boot/home/develop"
    echo "wget http://$host_ip:$port/apc_mini_project.zip"
    echo "unzip apc_mini_project.zip"
    echo "cd apc_mini_project/build"
    echo "make debug"
}

case "${1:-server}" in
    "server")
        start_http_server ${2:-8080}
        ;;
    "commands")
        generate_download_commands ${2:-10.0.2.2} ${3:-8080}
        ;;
    "help")
        echo "Usage: $0 [server|commands|help] [port]"
        echo "  server   - Start HTTP server (default)"
        echo "  commands - Show download commands for Haiku"
        echo "  help     - Show this help"
        ;;
    *)
        log_error "Unknown command: $1"
        exit 1
        ;;
esac
EOF

    chmod +x "$scripts_dir/transfer_files.sh"

    # VM management script
    log_info "Creating VM management script..."
    cat > "$scripts_dir/manage_vm.sh" << 'EOF'
#!/bin/bash

# VM Management Script for Haiku Development

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VM_DIR="$(dirname "$SCRIPT_DIR")"

log_info() {
    echo -e "\033[0;34m[INFO]\033[0m $1"
}

log_success() {
    echo -e "\033[0;32m[SUCCESS]\033[0m $1"
}

log_error() {
    echo -e "\033[0;31m[ERROR]\033[0m $1"
}

# Check VM status
check_vm_status() {
    local vm_processes=$(pgrep -f "qemu.*haiku" || true)

    if [ -n "$vm_processes" ]; then
        log_info "VM is running (PID: $vm_processes)"
        return 0
    else
        log_info "VM is not running"
        return 1
    fi
}

# Stop VM gracefully
stop_vm() {
    local vm_pids=$(pgrep -f "qemu.*haiku" || true)

    if [ -z "$vm_pids" ]; then
        log_info "VM is not running"
        return 0
    fi

    log_info "Stopping VM gracefully..."

    for pid in $vm_pids; do
        kill -TERM "$pid"
    done

    # Wait for graceful shutdown
    local timeout=30
    while [ $timeout -gt 0 ] && pgrep -f "qemu.*haiku" > /dev/null; do
        sleep 1
        timeout=$((timeout - 1))
    done

    # Force kill if still running
    local remaining_pids=$(pgrep -f "qemu.*haiku" || true)
    if [ -n "$remaining_pids" ]; then
        log_info "Force killing VM..."
        for pid in $remaining_pids; do
            kill -KILL "$pid"
        done
    fi

    log_success "VM stopped"
}

# Backup VM disk
backup_vm() {
    local disk_file="$VM_DIR/haiku-apc-mini-dev.qcow2"
    local backup_file="$VM_DIR/backup-$(date +%Y%m%d-%H%M%S).qcow2"

    if [ ! -f "$disk_file" ]; then
        log_error "VM disk not found: $disk_file"
        return 1
    fi

    log_info "Creating VM backup: $backup_file"
    cp "$disk_file" "$backup_file"
    log_success "Backup created: $backup_file"
}

# Reset VM (delete disk)
reset_vm() {
    local disk_file="$VM_DIR/haiku-apc-mini-dev.qcow2"

    if check_vm_status; then
        log_error "Cannot reset VM while it's running. Stop it first."
        return 1
    fi

    if [ -f "$disk_file" ]; then
        read -p "This will delete the VM disk. Are you sure? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm "$disk_file"
            log_success "VM disk deleted. Next start will trigger fresh installation."
        else
            log_info "Reset cancelled"
        fi
    else
        log_info "VM disk not found. Next start will trigger fresh installation."
    fi
}

case "${1:-status}" in
    "status")
        check_vm_status
        ;;
    "stop")
        stop_vm
        ;;
    "backup")
        backup_vm
        ;;
    "reset")
        reset_vm
        ;;
    "help")
        echo "Usage: $0 [status|stop|backup|reset|help]"
        echo "  status - Check VM status"
        echo "  stop   - Stop running VM"
        echo "  backup - Create VM disk backup"
        echo "  reset  - Delete VM disk (fresh install)"
        echo "  help   - Show this help"
        ;;
    *)
        log_error "Unknown command: $1"
        exit 1
        ;;
esac
EOF

    chmod +x "$scripts_dir/manage_vm.sh"

    log_success "Helper scripts created in $scripts_dir"
}

# Create shared directory with project files
setup_shared_files() {
    local shared_dir="$VM_DIR/shared"

    log_info "Setting up shared files..."

    # Copy project files to shared directory
    cp -r "$PROJECT_DIR" "$shared_dir/"

    # Create archive for easy transfer
    cd "$shared_dir"
    zip -r apc_mini_project.zip apc_mini_project/

    log_success "Project files prepared in: $shared_dir"
}

# Generate README for VM setup
generate_vm_readme() {
    local readme_file="$VM_DIR/README.md"

    cat > "$readme_file" << EOF
# Haiku VM for APC Mini Development

This directory contains a QEMU virtual machine setup for developing and testing the APC Mini application on Haiku OS.

## Files and Directories

- \`scripts/start_vm.sh\` - Main VM startup script
- \`scripts/transfer_files.sh\` - File transfer helper
- \`scripts/manage_vm.sh\` - VM management utilities
- \`iso/\` - Haiku installation ISO
- \`shared/\` - Files to transfer to VM
- \`${VM_NAME}.qcow2\` - VM disk image (created on first run)

## Quick Start

1. **Start VM:**
   \`\`\`bash
   ./scripts/start_vm.sh
   \`\`\`

2. **Install Haiku** (first run):
   - Follow the Haiku installation wizard
   - Install to the virtual hard drive
   - Reboot when installation completes

3. **Transfer project files:**
   \`\`\`bash
   # In host terminal:
   ./scripts/transfer_files.sh

   # In Haiku VM terminal:
   mkdir -p /boot/home/develop
   cd /boot/home/develop
   wget http://10.0.2.2:8080/apc_mini_project.zip
   unzip apc_mini_project.zip
   \`\`\`

4. **Build and test:**
   \`\`\`bash
   # In Haiku VM:
   cd /boot/home/develop/apc_mini_project/build
   make debug
   ./apc_mini_test_debug
   \`\`\`

## VM Configuration

- **Memory:** $MEMORY
- **CPUs:** $CPUS
- **Disk:** $DISK_SIZE
- **Network:** NAT with port forwarding (SSH: 2222, HTTP: 8080)
- **USB:** APC Mini passthrough (if detected)

## USB Passthrough

The VM is configured to pass through the APC Mini USB device (VID:PID 09e8:0028) if detected on the host system.

To verify USB passthrough:
1. Connect APC Mini to host
2. Start VM
3. In QEMU monitor: \`info usb\`
4. In Haiku: Check \`/dev/bus/usb/\`

## Management Commands

\`\`\`bash
# Check VM status
./scripts/manage_vm.sh status

# Stop VM gracefully
./scripts/manage_vm.sh stop

# Create backup
./scripts/manage_vm.sh backup

# Reset VM (fresh install)
./scripts/manage_vm.sh reset
\`\`\`

## File Transfer Methods

### Method 1: HTTP Server (Recommended)
\`\`\`bash
# Host:
./scripts/transfer_files.sh server

# Haiku:
wget http://10.0.2.2:8080/apc_mini_project.zip
\`\`\`

### Method 2: Serial Console
Files can be transferred via copy-paste in the serial console if needed.

## Troubleshooting

### VM Won't Start
- Check QEMU installation
- Verify virtualization support (KVM)
- Check available memory

### No USB Devices in VM
- Verify APC Mini connection on host
- Check host USB permissions
- Try restarting VM with device connected

### Network Issues
- VM uses NAT networking (10.0.2.0/24)
- Host IP from VM perspective: 10.0.2.2
- Port forwarding: SSH (2222), HTTP (8080)

### Performance Issues
- Enable KVM acceleration
- Allocate more memory if available
- Close unnecessary host applications

## Development Workflow

1. **Edit code** on host system
2. **Transfer files** to VM via HTTP server
3. **Build and test** in Haiku VM
4. **Debug** using Genio IDE or GDB in VM
5. **Iterate** as needed

## VM Snapshots

To create snapshots for quick rollback:
\`\`\`bash
# Create snapshot
qemu-img snapshot -c snapshot_name ${VM_NAME}.qcow2

# List snapshots
qemu-img snapshot -l ${VM_NAME}.qcow2

# Restore snapshot
qemu-img snapshot -a snapshot_name ${VM_NAME}.qcow2
\`\`\`

Generated by setup_qemu.sh on $(date)
EOF

    log_success "VM README created: $readme_file"
}

# Main setup function
main() {
    log_info "Starting QEMU setup for APC Mini development on Haiku OS"
    log_info "VM Name: $VM_NAME"
    log_info "VM Directory: $VM_DIR"

    check_prerequisites
    setup_vm_directory
    download_haiku
    create_vm_disk
    generate_startup_script
    generate_helper_scripts
    setup_shared_files
    generate_vm_readme

    log_success "QEMU setup completed successfully!"
    echo
    log_info "Next steps:"
    echo "1. Start the VM: $VM_DIR/scripts/start_vm.sh"
    echo "2. Install Haiku (first run)"
    echo "3. Transfer project files to VM"
    echo "4. Build and test APC Mini application"
    echo
    log_info "See $VM_DIR/README.md for detailed instructions"
}

# Script entry point
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi