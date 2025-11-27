# ESP32-S3 SSH Keyboard with WiFi Provisioning

A versatile ESP32-S3 implementation that transforms the board into a remote-controlled USB HID keyboard. This project combines multiple input methods including UART, WiFi provisioning, and SSH server capabilities to create a comprehensive remote typing solution.

## Features

### Core Functionality
- **USB HID Keyboard**: Acts as a standard USB HID keyboard when connected to any computer
- **Multiple Input Methods**: Supports UART, SSH server, and WiFi provisioning for flexible remote control
- **Comprehensive Character Support**: Full support for letters, numbers, punctuation, and special characters
- **Advanced Key Support**: Arrow keys (↑↓←→), function keys, Home/End, Page Up/Down, Tab, and more
- **Escape Sequence Handling**: Processes ANSI escape sequences and terminal-specific commands
- **Real-time Processing**: Instant character translation and USB output with natural typing delays

### Network & Remote Control
- **WiFi Provisioning**: Simple QR code-based WiFi setup using ESP Provisioning app
- **BLE Provisioning Support**: Bluetooth Low Energy provisioning for easy network configuration
- **SSH Server**: Full SSH server integration for remote keyboard control
- **Multiple SSH Clients**: Supports up to 3 simultaneous SSH connections
- **Remote Demo Mode**: Automatic typing demonstrations for testing purposes

### Development & Debugging
- **UART Debug Interface**: Detailed hex logging for debugging character reception
- **Escape Sequence Detection**: Advanced parsing for terminal navigation keys
- **Shift Key Handling**: Automatic shift key management for uppercase and special characters
- **Error Handling**: Robust buffer overflow protection and error recovery

## Hardware Requirements

- **ESP32-S3 Board**: Any ESP32-S3 with native USB support (ESP32-S3-USB-OTG recommended)
- **USB Type-C Cable**: For connection to target computer
- **Development Computer**: For flashing, monitoring, and SSH access
- **WiFi Network**: 2.4GHz network for provisioning and SSH access

## Software Requirements

- **ESP-IDF v5.5**: Required version (not 6.0)
- **ESP32-S3 Toolchain**: Compatible toolchain for ESP32-S3
- **Dependencies**: Automatically managed via Component Manager

## Project Structure

This project includes multiple firmware variants for different use cases:

### Available Firmware Variants

1. **`esp32-wifi-keyboard.c`** - **Recommended Main Version**
   - Complete USB HID keyboard functionality
   - UART input support (via `idf.py monitor`)
   - Advanced escape sequence handling for arrow keys and navigation
   - Comprehensive character mapping with shift key support
   - Debug logging for troubleshooting

2. **`wifi-prov-keyboard.c`** - **WiFi Provisioning Version**
   - All features from main version plus WiFi connectivity
   - QR code-based WiFi provisioning using ESP Provisioning app
   - BLE provisioning support
   - Remote keyboard demonstration mode
   - Automatic IP address display via USB keyboard

3. **`ssh-keyboard.c`** - **SSH Server Version**
   - Full SSH server integration for remote control
   - USB HID keyboard + SSH server capabilities
   - WiFi connectivity for remote access
   - Support for multiple SSH clients (up to 3)
   - SSH authentication with username/password

4. **`ssh-keyboard-simple.c`** - **SSH Simplified Version**
   - Demonstrates SSH concept without full libssh integration
   - Simulated remote typing for testing
   - Hardcoded WiFi credentials (update required)
   - Foundation for adding complete SSH server

## Installation and Setup

### 1. Prerequisites
```bash
# Ensure ESP-IDF v5.5 is installed and configured
# Export ESP-IDF environment
source $HOME/esp/esp-idf/export.sh
```

### 2. Project Configuration
The project includes pre-configured dependencies in `main/idf_component.yml`:
- `espressif/esp_tinyusb: ^2.0.1` - High-level TinyUSB wrapper
- `espressif/cjson: ^1.7.19` - JSON parsing for WiFi provisioning
- `espressif/qrcode: ^0.1.0~2` - QR code generation
- `espressif/network_provisioning: ^1.2.0` - WiFi provisioning manager
- `david-cermak/libssh: 0.11.0~1` - SSH server library

### 3. Select Firmware Variant
Choose the appropriate source file and rename it to match your CMakeLists.txt configuration, or modify the build configuration.

### 4. Build and Flash
```bash
# Build the project
idf.py build

# Flash to ESP32-S3
idf.py flash

# Monitor UART output (for debugging and UART input)
idf.py monitor
```

## Usage Instructions

### Basic USB Keyboard Usage (All Versions)
1. **Connect ESP32-S3** to your target computer via USB
2. **Flash the firmware** using `idf.py flash`
3. **Type via UART**: Use `idf.py monitor` to send characters
4. **Characters appear** as keyboard input on the connected computer

### WiFi Provisioning (wifi-prov-keyboard.c)
1. **Flash firmware** and monitor output
2. **Scan QR code** displayed in serial monitor with ESP Provisioning app
3. **Select WiFi network** and enter credentials
4. **Device connects** and displays IP address via USB keyboard
5. **Remote demo** automatically types demonstration messages

### SSH Remote Control (ssh-keyboard.c)
1. **Configure WiFi** (provisioning or hardcoded credentials)
2. **Connect via SSH**: `ssh esp32@<device_ip>` (password: keyboard)
3. **Type commands** in SSH session - they appear as keyboard input
4. **Multiple users** can connect simultaneously (up to 3 clients)

### Advanced Key Support
All versions support comprehensive keyboard input:

```bash
# Basic typing
hello world          # Types: hello world
HELLO WORLD          # Types: HELLO WORLD (with shift)
123!@#$              # Types: 123!@#$

# Special keys
Tab                  # Inserts tab character
Enter                # New line
Backspace            # Deletes character

# Arrow keys and navigation
↑ ↓ ← →              # Arrow navigation
Home/End            # Line navigation
Page Up/Down        # Page navigation
Insert/Delete       # Text editing
```

### Debugging and Monitoring
```bash
# Monitor with verbose output
idf.py monitor -b 115200

# View hex debug output for key sequences
# Example: Arrow up shows as: "1b 5b 41" (ESC [ A)
```

## Technical Implementation

### Architecture Overview
```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  Input Sources  │ →  │   Processing     │ →  │ USB HID Output  │
│                 │    │                  │    │                 │
│ • UART          │    │ • Char Mapping   │    │ • Keyboard HID  │
│ • SSH Server    │    │ • Escape Seq     │    │ • TinyUSB Stack │
│ • WiFi Prov     │    │ • Shift Handling │    │ • USB Reports   │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

### Core Components

#### 1. USB HID Subsystem
- **TinyUSB Integration**: ESP32-specific wrapper for native USB support
- **HID Descriptors**: Standard USB HID keyboard descriptors
- **Key Mapping**: Comprehensive ASCII-to-HID keycode conversion
- **Modifier Support**: Automatic shift key handling for uppercase/special chars

#### 2. Input Processing Engine
- **UART Driver**: Event-driven UART with configurable buffers
- **Escape Sequence Parser**: Handles ANSI escape sequences and terminal navigation
- **Character Mapper**: Converts input characters to HID keycodes
- **Multi-input Support**: Unified processing for UART, SSH, and network inputs

#### 3. Network & Remote Control
- **WiFi Provisioning**: QR code-based setup using ESP Provisioning framework
- **BLE Provisioning**: Bluetooth Low Energy for easy network configuration
- **SSH Server**: Full libssh integration for remote keyboard control
- **Multi-client Support**: Handles multiple simultaneous SSH connections

The SSH server implementation demonstrates advanced ESP-IDF component porting techniques. For detailed information about the libssh integration approach and methodology for porting third-party libraries to ESP-IDF, see the official Espressif guide: [Advanced Porting: Libraries as Components](https://developer.espressif.com/blog/2025/11/advanced-porting-libraries-as-components/).

#### 4. FreeRTOS Integration
- **Task-based Architecture**: Separate tasks for UART, SSH, and USB processing
- **Queue Management**: Thread-safe communication between input sources
- **Event Groups**: Synchronization for WiFi connection status
- **Memory Management**: Efficient buffer allocation and cleanup

### Firmware Architecture Comparison

| Component | Basic | WiFi Prov | SSH Server | SSH Simple |
|-----------|--------|-----------|------------|------------|
| USB HID   | ✓      | ✓         | ✓          | ✓          |
| UART Input| ✓      | ✓         | ✓          | ✓          |
| WiFi      | ✗      | ✓         | ✓          | ✓          |
| SSH Server| ✗      | ✗         | ✓          | Simulated  |
| Provisioning| ✗    | ✓         | ✗          | ✗          |
| Multi-client| ✗     | ✗         | ✓          | ✗          |

### Project Structure
```
esp32-s3-ssh-keyboard/
├── main/
│   ├── esp32-wifi-keyboard.c      # Main UART-based version
│   ├── wifi-prov-keyboard.c       # WiFi provisioning version
│   ├── ssh-keyboard.c            # Full SSH server version
│   ├── ssh-keyboard-simple.c     # Simplified SSH demo
│   ├── CMakeLists.txt            # Build configuration
│   └── idf_component.yml         # Component dependencies
├── CMakeLists.txt                # Project configuration
├── README.md                     # This documentation
└── sdkconfig.defaults           # ESP32-S3 configuration
```

### Configuration Details

#### ESP-IDF Configuration (sdkconfig.defaults)
```bash
# Target platform
CONFIG_IDF_TARGET_ESP32S3=y

# TinyUSB configuration
CONFIG_TINYUSB_HID_COUNT=1        # Enable 1 HID interface
CONFIG_TINYUSB_CDC_COUNT=0        # Disable CDC (not needed)
CONFIG_TINYUSB_MSC_ENABLED=n      # Disable MSC (not needed)

# Performance tuning
CONFIG_FREERTOS_HZ=1000           # 1ms tick resolution
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10  # 10-second task watchdog
```

#### Component Dependencies (idf_component.yml)
```yaml
dependencies:
  idf:
    version: '>=4.1.0'           # Minimum ESP-IDF version
  espressif/esp_tinyusb: ^2.0.1   # TinyUSB wrapper
  espressif/cjson: ^1.7.19        # JSON parsing
  espressif/qrcode: ^0.1.0~2     # QR code generation
  espressif/network_provisioning: ^1.2.0  # WiFi provisioning
  david-cermak/libssh: 0.11.0~1   # SSH server library
```

## Development and Debugging

### Comprehensive Debugging Features
The firmware provides extensive debug output for development and troubleshooting:

```bash
# Enable detailed logging
idf.py monitor -b 115200

# Example debug output for arrow keys:
I (xxxx) esp32_keyboard: Received 3 bytes via UART: 1b 5b 41
# This shows an arrow up sequence (ESC [ A)
```

### Debug Output Categories

#### 1. Input Reception Logging
- **UART Data**: All received bytes displayed in hexadecimal format
- **SSH Data**: Network input logging with connection status
- **WiFi Events**: Connection, provisioning, and IP assignment status

#### 2. Processing Pipeline
- **Character Mapping**: Shows how characters are converted to HID codes
- **Escape Sequences**: Displays detected navigation sequences
- **Shift Key Logic**: Indicates modifier key activation

#### 3. USB HID Status
- **Connection Status**: USB enumeration and mounting events
- **Report Transmission**: Keyboard report sending confirmation
- **Error Conditions**: USB communication failures

### Common Development Workflows

#### 1. Testing Character Input
```bash
# Monitor for character reception
idf.py monitor

# Test sequences to type:
# Basic text: hello world
# Uppercase: HELLO WORLD (observes shift activation)
# Numbers: 1234567890
# Special chars: !@#$%^&*()
# Navigation: Use arrow keys, watch for hex sequences
```

#### 2. Network Configuration Testing
```bash
# For WiFi provisioning versions:
# 1. Flash firmware
# 2. Monitor serial output for QR code
# 3. Scan with ESP Provisioning app
# 4. Watch for connection status and IP assignment

# For SSH versions:
# 1. Connect to WiFi
# 2. Note device IP from serial monitor
# 3. Test SSH connection: ssh esp32@<IP>
# 4. Type in SSH session, observe USB output
```

#### 3. USB HID Verification
```bash
# Test USB recognition on host computer:
# 1. Check device manager for HID keyboard
# 2. Open text editor
# 3. Type via monitor/SSH - verify characters appear
# 4. Test special keys (Tab, Enter, arrows)
```

## Troubleshooting Guide

### Build and Flash Issues

#### ESP-IDF Environment
```bash
# Verify ESP-IDF setup
idf.py --version
# Should show ESP-IDF v5.5

# Check target configuration
idf.py menuconfig
# Verify: ESP32-S3 target selected
```

#### Dependency Resolution
```bash
# Clean and rebuild
idf.py fullclean
idf.py build

# Update component dependencies
idf.py reconfigure
```

### Hardware and USB Issues

#### USB Connection Problems
- **Cable Issues**: Try high-quality USB-C cable
- **Port Issues**: Test different USB ports on host computer
- **Power**: Ensure adequate power supply
- **Driver Issues**: Check OS HID keyboard drivers

#### ESP32-S3 Board Issues
- **Native USB**: Confirm board has native USB support
- **Boot Mode**: Verify board is in correct boot mode
- **Power Cycling**: Reset board after flashing

### Network and Connectivity Issues

#### WiFi Provisioning Failures
```bash
# Check provisioning app compatibility
# Verify 2.4GHz WiFi network availability
# Ensure network credentials are correct
# Monitor serial output for error messages
```

#### SSH Connection Problems
```bash
# Verify WiFi connection status
# Check device IP address in serial monitor
# Test network connectivity: ping <device_ip>
# Verify SSH credentials (esp32/keyboard)
# Check firewall settings on host computer
```

### Input and Character Issues

#### Character Mapping Problems
- **Encoding**: Ensure UTF-8/ASCII input
- **Debug Mode**: Monitor hex output for received bytes
- **Escape Sequences**: Verify terminal sends proper sequences
- **Special Keys**: Test individually to isolate issues

#### Keyboard Output Problems
- **USB Mounting**: Check for USB enumeration messages
- **HID Reports**: Monitor for report transmission logs
- **Host Recognition**: Verify OS detects HID keyboard device

## Performance Optimization

### Memory Configuration
```bash
# Adjust buffer sizes in firmware if needed
#define BUF_SIZE (1024)     // UART buffer size
#define SSH_BUFFER_SIZE 1024 // SSH input buffer
```

### Task Priorities
- **UART Task**: Priority 12 (high priority for real-time input)
- **SSH Task**: Priority 5 (medium priority)
- **USB Task**: Handled by TinyUSB stack

### Network Timing
- **WiFi Connection**: 30-second timeout for provisioning
- **SSH Authentication**: Immediate response for credentials
- **Retransmission**: Automatic retry for network failures

## Security Considerations

### SSH Security
- **Authentication**: Username/password authentication (configurable)
- **Default Credentials**: Change from defaults in production
- **Network Isolation**: Use firewall to restrict access
- **Key Management**: Replace hardcoded keys with secure key storage

### WiFi Security
- **Provisioning Security**: Use WPA2/WPA3 networks
- **Network Isolation**: Consider guest network for device
- **Credential Storage**: Credentials stored in NVS (encrypted)

### USB Security
- **Device Identification**: USB descriptors identify as ESP32 keyboard
- **Input Validation**: All input processed before USB transmission
- **Physical Security**: Device requires physical USB connection

## Contributing Guidelines

### Development Environment Setup
1. **ESP-IDF v5.5**: Ensure correct version is installed
2. **Hardware**: ESP32-S3 development board with USB support
3. **Testing**: Test all input methods (UART, SSH, WiFi)
4. **Documentation**: Update README for new features

### Areas for Enhancement
- **Additional Keyboard Layouts**: International keyboard support
- **Mouse Integration**: USB HID mouse functionality
- **Enhanced Security**: SSH key-based authentication
- **Web Interface**: Browser-based remote control
- **Mobile App**: Dedicated mobile application for remote control
- **Cloud Integration**: Remote access through cloud services

### Code Contributions
- **Style Guidelines**: Follow ESP-IDF coding conventions
- **Testing**: Include comprehensive testing for new features
- **Documentation**: Document new functionality and configuration options
- **Backward Compatibility**: Maintain compatibility with existing versions

## License and Credits

### License
This project is provided as-is for educational and development purposes.

### Technology Credits
- **ESP-IDF Framework**: Espressif Systems development framework
- **TinyUSB Stack**: Ha Thach's lightweight USB stack
- **libssh**: SSH server implementation by Andreas Schneider
- **FreeRTOS**: Real-time operating system for microcontrollers

### SSH Implementation Details
The SSH server functionality leverages libssh ported as an ESP-IDF component. For detailed information about porting libraries as components in ESP-IDF, including the libssh integration approach used in this project, see the Espressif blog post: [Advanced Porting: Libraries as Components](https://developer.espressif.com/blog/2025/11/advanced-porting-libraries-as-components/).

This article covers the methodology for integrating third-party libraries like libssh into ESP-IDF projects, including dependency management, build configuration, and platform-specific optimizations that enable SSH server functionality on ESP32 devices.

### Hardware Compatibility
- **Primary Development**: ESP32-S3-USB-OTG development board
- **Supported Boards**: Any ESP32-S3 variant with native USB support
- **Tested Platforms**: Windows, macOS, Linux host systems

### Project Inspiration
This project builds upon ESP-IDF TinyUSB examples and extends them with comprehensive remote control capabilities, WiFi provisioning, and SSH server integration for advanced use cases.