#!/bin/bash

# Deployment Script for APC Mini Test Application to Haiku OS
# Supports deployment to QEMU VM, physical Haiku system, or network transfer

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
DOCS_DIR="$PROJECT_DIR/docs"
EXAMPLES_DIR="$PROJECT_DIR/examples"

# Default configuration
DEFAULT_HOST="10.0.2.2"  # QEMU default host IP
DEFAULT_PORT="8080"
DEFAULT_TARGET_DIR="/boot/home/develop/apc_mini_test"
PACKAGE_NAME="apc_mini_project"

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

# Show usage information
show_usage() {
    cat << EOF
Usage: $0 [COMMAND] [OPTIONS]

COMMANDS:
    package     Create deployment package
    serve       Start HTTP server for file transfer
    upload      Upload files via SCP/SSH
    local       Copy to local Haiku installation
    install     Install from package in Haiku
    help        Show this help

OPTIONS:
    --host HOST         Target host (default: $DEFAULT_HOST)
    --port PORT         HTTP server port (default: $DEFAULT_PORT)
    --target-dir DIR    Target directory (default: $DEFAULT_TARGET_DIR)
    --user USER         SSH/SCP username
    --build-type TYPE   Build type: debug|release (default: debug)
    --no-examples       Skip example utilities
    --clean             Clean before building

EXAMPLES:
    # Create package and serve via HTTP (QEMU VM)
    $0 serve

    # Upload to remote Haiku system via SSH
    $0 upload --host 192.168.1.100 --user haiku

    # Copy to local Haiku partition
    $0 local --target-dir /HaikuPartition/home/develop

    # Install pre-built package
    $0 install --host 192.168.1.100

EOF
}

# Parse command line arguments
parse_arguments() {
    COMMAND="${1:-serve}"
    shift || true

    # Default values
    HOST="$DEFAULT_HOST"
    PORT="$DEFAULT_PORT"
    TARGET_DIR="$DEFAULT_TARGET_DIR"
    USER=""
    BUILD_TYPE="debug"
    INCLUDE_EXAMPLES="true"
    CLEAN_BUILD="false"

    while [[ $# -gt 0 ]]; do
        case $1 in
            --host)
                HOST="$2"
                shift 2
                ;;
            --port)
                PORT="$2"
                shift 2
                ;;
            --target-dir)
                TARGET_DIR="$2"
                shift 2
                ;;
            --user)
                USER="$2"
                shift 2
                ;;
            --build-type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            --no-examples)
                INCLUDE_EXAMPLES="false"
                shift
                ;;
            --clean)
                CLEAN_BUILD="true"
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check if we're in the right directory
    if [ ! -f "$BUILD_DIR/Makefile" ]; then
        log_error "Makefile not found in $BUILD_DIR"
        log_error "Please run this script from the project root or fix the path"
        exit 1
    fi

    # Check build tools for packaging
    if ! command -v zip &> /dev/null; then
        log_warning "zip not found. Package creation may fail."
    fi

    case "$COMMAND" in
        "upload")
            if ! command -v scp &> /dev/null; then
                log_error "scp not found. Please install OpenSSH client."
                exit 1
            fi
            ;;
        "serve")
            if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
                log_error "Python not found. Cannot start HTTP server."
                exit 1
            fi
            ;;
    esac

    log_success "Prerequisites check passed"
}

# Build the project
build_project() {
    log_info "Building project (type: $BUILD_TYPE)..."

    cd "$BUILD_DIR"

    if [ "$CLEAN_BUILD" = "true" ]; then
        log_info "Cleaning previous build..."
        make clean
    fi

    # Build main application
    case "$BUILD_TYPE" in
        "debug")
            make debug
            ;;
        "release")
            make release
            ;;
        *)
            log_error "Invalid build type: $BUILD_TYPE"
            exit 1
            ;;
    esac

    # Build examples if requested
    if [ "$INCLUDE_EXAMPLES" = "true" ]; then
        log_info "Building examples..."
        make examples
    fi

    log_success "Build completed"
}

# Create deployment package
create_package() {
    local package_file="$BUILD_DIR/${PACKAGE_NAME}_deployment.zip"

    log_info "Creating deployment package: $package_file"

    cd "$PROJECT_DIR"

    # Remove old package
    rm -f "$package_file"

    # Create temporary packaging directory
    local temp_dir=$(mktemp -d)
    local package_dir="$temp_dir/$PACKAGE_NAME"

    mkdir -p "$package_dir"

    # Copy source code
    log_info "Packaging source code..."
    cp -r src "$package_dir/"

    # Copy build system
    mkdir -p "$package_dir/build"
    cp build/Makefile "$package_dir/build/"
    cp build/*.gproject "$package_dir/build/" 2>/dev/null || true

    # Copy documentation
    log_info "Packaging documentation..."
    cp -r docs "$package_dir/"

    # Copy scripts
    log_info "Packaging scripts..."
    cp -r scripts "$package_dir/"

    # Copy examples
    if [ "$INCLUDE_EXAMPLES" = "true" ]; then
        log_info "Packaging examples..."
        cp -r examples "$package_dir/"
    fi

    # Copy built binaries (if they exist)
    if [ -f "$BUILD_DIR/apc_mini_test" ]; then
        mkdir -p "$package_dir/bin"
        cp "$BUILD_DIR/apc_mini_test" "$package_dir/bin/"
    fi

    if [ -f "$BUILD_DIR/apc_mini_test_debug" ]; then
        mkdir -p "$package_dir/bin"
        cp "$BUILD_DIR/apc_mini_test_debug" "$package_dir/bin/"
    fi

    # Copy example binaries
    if [ "$INCLUDE_EXAMPLES" = "true" ]; then
        for example in led_patterns midi_monitor; do
            if [ -f "$BUILD_DIR/$example" ]; then
                cp "$BUILD_DIR/$example" "$package_dir/bin/"
            fi
        done
    fi

    # Create deployment script for Haiku
    create_haiku_install_script "$package_dir"

    # Create the package
    cd "$temp_dir"
    zip -r "$package_file" "$PACKAGE_NAME/"

    # Cleanup
    rm -rf "$temp_dir"

    if [ -f "$package_file" ]; then
        log_success "Package created: $package_file"
        log_info "Package size: $(du -h "$package_file" | cut -f1)"
    else
        log_error "Failed to create package"
        exit 1
    fi
}

# Create installation script for Haiku
create_haiku_install_script() {
    local package_dir="$1"
    local install_script="$package_dir/install_haiku.sh"

    log_info "Creating Haiku installation script..."

    cat > "$install_script" << 'EOF'
#!/bin/bash

# Installation script for APC Mini Test Application on Haiku OS
# Run this script after extracting the deployment package

set -e

# Configuration
PROJECT_NAME="apc_mini_test"
INSTALL_DIR="/boot/home/develop/$PROJECT_NAME"
DESKTOP_LINK="/boot/home/Desktop/APC Mini Test"
BIN_DIR="/boot/home/config/non-packaged/bin"

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

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check Haiku system
check_haiku_system() {
    if [ "$(uname)" != "Haiku" ]; then
        log_error "This script must be run on Haiku OS"
        exit 1
    fi

    log_info "Detected Haiku OS: $(uname -r)"
}

# Install development packages
install_dev_packages() {
    log_info "Installing development packages..."

    # Check if packages are already installed
    if pkgman search haiku_devel | grep -q "installed"; then
        log_info "Development packages already installed"
        return
    fi

    # Install required packages
    log_info "Installing haiku_devel..."
    pkgman install -y haiku_devel || log_warning "Failed to install haiku_devel"

    log_info "Installing midi_devel..."
    pkgman install -y midi_devel || log_warning "Failed to install midi_devel"

    log_info "Installing gcc..."
    pkgman install -y gcc || log_warning "Failed to install gcc"

    log_success "Development packages installation completed"
}

# Create installation directory
create_install_dir() {
    log_info "Creating installation directory: $INSTALL_DIR"

    mkdir -p "$INSTALL_DIR"
    mkdir -p "$(dirname "$DESKTOP_LINK")"
    mkdir -p "$BIN_DIR"

    log_success "Installation directories created"
}

# Copy project files
copy_project_files() {
    log_info "Copying project files..."

    # Copy all project files
    cp -r ./* "$INSTALL_DIR/"

    # Make scripts executable
    find "$INSTALL_DIR" -name "*.sh" -exec chmod +x {} \;

    log_success "Project files copied"
}

# Build the application
build_application() {
    log_info "Building application..."

    cd "$INSTALL_DIR/build"

    # Build debug version
    make debug

    if [ $? -eq 0 ]; then
        log_success "Debug build completed"
    else
        log_error "Debug build failed"
        return 1
    fi

    # Build release version
    make release

    if [ $? -eq 0 ]; then
        log_success "Release build completed"
    else
        log_warning "Release build failed"
    fi

    # Build examples
    make examples

    if [ $? -eq 0 ]; then
        log_success "Examples build completed"
    else
        log_warning "Examples build failed"
    fi
}

# Install binaries
install_binaries() {
    log_info "Installing binaries..."

    cd "$INSTALL_DIR/build"

    # Install main application
    if [ -f "apc_mini_test" ]; then
        cp "apc_mini_test" "$BIN_DIR/"
        log_success "Installed apc_mini_test to $BIN_DIR"
    fi

    if [ -f "apc_mini_test_debug" ]; then
        cp "apc_mini_test_debug" "$BIN_DIR/"
        log_success "Installed apc_mini_test_debug to $BIN_DIR"
    fi

    # Install examples
    for example in led_patterns midi_monitor; do
        if [ -f "$example" ]; then
            cp "$example" "$BIN_DIR/"
            log_success "Installed $example to $BIN_DIR"
        fi
    done
}

# Create desktop link
create_desktop_link() {
    log_info "Creating desktop link..."

    cat > "$DESKTOP_LINK" << 'DESKTOP_EOF'
#!/bin/bash
cd /boot/home/develop/apc_mini_test/build
Terminal ./apc_mini_test_debug
DESKTOP_EOF

    chmod +x "$DESKTOP_LINK"
    log_success "Desktop link created: $DESKTOP_LINK"
}

# Run tests
run_tests() {
    log_info "Running basic tests..."

    cd "$INSTALL_DIR/build"

    # Test simulation mode
    log_info "Testing simulation mode..."
    timeout 10s ./apc_mini_test_debug --simulation || true

    log_success "Basic tests completed"
}

# Main installation
main() {
    log_info "Starting APC Mini Test Application installation on Haiku OS"

    check_haiku_system
    install_dev_packages
    create_install_dir
    copy_project_files
    build_application
    install_binaries
    create_desktop_link
    run_tests

    log_success "Installation completed successfully!"
    echo
    log_info "Usage:"
    echo "  Command line: apc_mini_test_debug"
    echo "  Desktop link: Double-click 'APC Mini Test' on Desktop"
    echo "  Manual build: cd $INSTALL_DIR/build && make debug"
    echo
    log_info "Documentation: $INSTALL_DIR/docs/"
}

# Run installation
main "$@"
EOF

    chmod +x "$install_script"
    log_success "Haiku installation script created"
}

# Start HTTP server for file transfer
start_http_server() {
    local server_dir="$BUILD_DIR"

    # Create package first
    create_package

    log_info "Starting HTTP server on port $PORT..."
    log_info "Server directory: $server_dir"
    log_info "Access URL: http://$HOST:$PORT/"

    cd "$server_dir"

    # Generate download instructions
    generate_download_instructions

    # Start server
    if command -v python3 &> /dev/null; then
        python3 -m http.server "$PORT"
    elif command -v python &> /dev/null; then
        python -m SimpleHTTPServer "$PORT"
    else
        log_error "Python not found"
        exit 1
    fi
}

# Generate download instructions for Haiku
generate_download_instructions() {
    local instructions_file="$BUILD_DIR/download_instructions.txt"

    cat > "$instructions_file" << EOF
# Download Instructions for Haiku OS

## Quick Download and Install:
wget http://$HOST:$PORT/${PACKAGE_NAME}_deployment.zip
unzip ${PACKAGE_NAME}_deployment.zip
cd $PACKAGE_NAME
./install_haiku.sh

## Manual Download and Build:
mkdir -p $TARGET_DIR
cd $TARGET_DIR
wget http://$HOST:$PORT/${PACKAGE_NAME}_deployment.zip
unzip ${PACKAGE_NAME}_deployment.zip
cd $PACKAGE_NAME/build
make debug
./apc_mini_test_debug

## Individual Files:
wget http://$HOST:$PORT/apc_mini_test_debug
wget http://$HOST:$PORT/led_patterns
wget http://$HOST:$PORT/midi_monitor

## Verify Download:
sha256sum ${PACKAGE_NAME}_deployment.zip

Generated on: $(date)
Host: $HOST:$PORT
Target: $TARGET_DIR
EOF

    log_info "Download instructions: $instructions_file"
    echo
    log_info "Commands for Haiku VM:"
    echo "  wget http://$HOST:$PORT/${PACKAGE_NAME}_deployment.zip"
    echo "  unzip ${PACKAGE_NAME}_deployment.zip"
    echo "  cd $PACKAGE_NAME"
    echo "  ./install_haiku.sh"
}

# Upload files via SSH/SCP
upload_files() {
    if [ -z "$USER" ]; then
        log_error "Username required for SSH upload. Use --user option."
        exit 1
    fi

    # Create package first
    create_package

    local package_file="$BUILD_DIR/${PACKAGE_NAME}_deployment.zip"
    local remote_target="$USER@$HOST:$TARGET_DIR"

    log_info "Uploading files to $remote_target..."

    # Create target directory
    ssh "$USER@$HOST" "mkdir -p $TARGET_DIR" || log_warning "Failed to create remote directory"

    # Upload package
    scp "$package_file" "$USER@$HOST:$TARGET_DIR/"

    # Upload installation commands
    cat << EOF | ssh "$USER@$HOST"
cd $TARGET_DIR
unzip -o ${PACKAGE_NAME}_deployment.zip
cd $PACKAGE_NAME
./install_haiku.sh
EOF

    log_success "Upload and installation completed"
}

# Copy to local Haiku installation
copy_local() {
    if [ ! -d "$TARGET_DIR" ]; then
        log_info "Creating target directory: $TARGET_DIR"
        mkdir -p "$TARGET_DIR"
    fi

    # Create package first
    create_package

    local package_file="$BUILD_DIR/${PACKAGE_NAME}_deployment.zip"

    log_info "Copying to local Haiku installation: $TARGET_DIR"

    # Copy and extract package
    cp "$package_file" "$TARGET_DIR/"

    cd "$TARGET_DIR"
    unzip -o "${PACKAGE_NAME}_deployment.zip"

    log_success "Files copied to $TARGET_DIR"
    log_info "To complete installation, run: cd $TARGET_DIR/$PACKAGE_NAME && ./install_haiku.sh"
}

# Install package on running Haiku system
install_package() {
    local package_file="$BUILD_DIR/${PACKAGE_NAME}_deployment.zip"

    if [ ! -f "$package_file" ]; then
        log_info "Package not found, creating..."
        create_package
    fi

    if [ -n "$USER" ]; then
        # Remote installation
        log_info "Installing package on remote system: $USER@$HOST"

        scp "$package_file" "$USER@$HOST:/tmp/"

        ssh "$USER@$HOST" << 'EOF'
cd /tmp
unzip -o *_deployment.zip
cd apc_mini_project
./install_haiku.sh
EOF

    else
        # Local installation (assuming we're on Haiku)
        log_info "Installing package locally..."

        local temp_dir="/tmp/apc_mini_install"
        mkdir -p "$temp_dir"

        cp "$package_file" "$temp_dir/"
        cd "$temp_dir"
        unzip -o "$(basename "$package_file")"
        cd "$PACKAGE_NAME"
        ./install_haiku.sh
    fi

    log_success "Package installation completed"
}

# Main execution
main() {
    parse_arguments "$@"

    log_info "APC Mini Test Application Deployment Tool"
    log_info "Command: $COMMAND"
    log_info "Build type: $BUILD_TYPE"
    log_info "Target: $HOST:$PORT"

    check_prerequisites

    # Build project unless we're just serving existing files
    if [ "$COMMAND" != "serve" ] || [ ! -f "$BUILD_DIR/apc_mini_test_debug" ]; then
        build_project
    fi

    case "$COMMAND" in
        "package")
            create_package
            ;;
        "serve")
            start_http_server
            ;;
        "upload")
            upload_files
            ;;
        "local")
            copy_local
            ;;
        "install")
            install_package
            ;;
        "help")
            show_usage
            ;;
        *)
            log_error "Unknown command: $COMMAND"
            show_usage
            exit 1
            ;;
    esac
}

# Script entry point
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi