# ESP32-S3 USB Keyboard with UART Input

An ESP32-S3 implementation that converts UART input into USB HID keyboard output, allowing you to type on your computer remotely through the ESP32-S3 board.

## Features

- **USB HID Keyboard**: Acts as a standard USB HID keyboard when connected to a computer
- **UART Input**: Receives keyboard input via UART (using `idf.py monitor`)
- **Character Mapping**: Comprehensive support for letters, numbers, punctuation, and special characters
- **Arrow Key Support**: Full support for arrow keys (↑↓←→) and other navigation keys
- **Escape Sequence Handling**: Supports both ANSI escape sequences and terminal-specific sequences
- **Real-time Processing**: Instant character translation and USB output
- **Shift Key Support**: Proper handling of uppercase letters and special characters requiring shift

## Hardware Requirements

- ESP32-S3-USB-OTG board (or any ESP32-S3 with native USB support)
- USB Type-C cable (for connection to computer)
- Computer for USB connection and UART monitoring

## Software Requirements

- ESP-IDF v6.0 or later
- ESP32-S3 toolchain
- Component Manager dependencies (automatically handled)

## Supported Keys

### Standard Characters
- **Letters**: a-z, A-Z (with proper shift handling)
- **Numbers**: 0-9
- **Punctuation**: !@#$%^&*()_-+=[]{}|;:'",.<>/?`
- **Special**: Space, Enter, Tab, Backspace

### Navigation Keys
- **Arrow Keys**: ↑ ↓ ← → (Up, Down, Left, Right)
- **Home/End**: Home and End key support
- **Function Keys**: Insert, Delete, Page Up, Page Down

### Escape Sequence Support
- **ANSI Sequences**: ESC [A (up), ESC [B (down), etc.
- **Terminal Sequences**: [A [B [C [D (from `idf.py monitor` arrow keys)
- **Extended Keys**: ESC [1~, ESC [2~, etc. for Home, End, Insert, Delete

## Installation and Setup

### 1. Clone and Configure
```bash
# If starting from scratch, create ESP-IDF project
idf.py create-project esp32-usb-keyboard
cd esp32-usb-keyboard

# Copy the project files into the main directory
```

### 2. Dependencies
The project uses the following ESP-IDF components:
- `espressif/esp_tinyusb^2.0.1` - High-level TinyUSB wrapper
- `espressif/tinyusb^0.19.0~2` - TinyUSB stack (automatically included)

Dependencies are automatically managed via the `main/idf_component.yml` file.

### 3. Build Configuration
The project includes `sdkconfig.defaults` with the following key settings:
```
CONFIG_IDF_TARGET_ESP32S3=y
CONFIG_TINYUSB_HID_COUNT=1
CONFIG_TINYUSB_CDC_COUNT=0
CONFIG_TINYUSB_MSC_ENABLED=n
```

### 4. Build and Flash
```bash
# Build the project
idf.py build

# Flash to ESP32-S3
idf.py flash

# Start monitor for UART input
idf.py monitor
```

## Usage

### Basic Usage
1. **Connect Hardware**: Connect ESP32-S3 to your computer via USB
2. **Flash Firmware**: Upload the firmware using `idf.py flash`
3. **Start Monitor**: Run `idf.py monitor` to enable UART input
4. **Type**: Type characters in the monitor - they appear as keyboard input on your computer!

### Arrow Keys and Navigation
- **Arrow Keys**: Use arrow keys in your terminal - they're translated to USB arrow keys
- **Home/End**: Use Home/End keys for navigation
- **Function Keys**: Page Up/Down, Insert, Delete are supported

### Character Examples
```bash
# In idf.py monitor, type:
hello world          # Appears as: hello world
HELLO WORLD          # Appears as: HELLO WORLD (with shift)
123!@#$              # Appears as: 123!@#$
# Use arrow keys to navigate in text editors
[Arrow Up]           # Moves cursor up
[Arrow Down]         # Moves cursor down
[Arrow Left]         # Moves cursor left
[Arrow Right]        # Moves cursor right
```

## Technical Implementation

### Architecture
```
UART Input → Event Processing → Character Mapping → USB HID Output
```

### Key Components
1. **UART Event Handler**: Processes incoming UART data with event-driven architecture
2. **Escape Sequence Parser**: Handles arrow keys and special keys from terminal input
3. **Character Mapper**: Converts characters to HID keycodes with shift key handling
4. **USB HID Interface**: Sends keyboard reports via TinyUSB stack
5. **TinyUSB Integration**: Uses ESP-specific TinyUSB wrapper for USB communication

### File Structure
```
esp32-wifi-keyboard/
├── main/
│   ├── esp32-wifi-keyboard.c    # Main application code
│   ├── CMakeLists.txt           # Component dependencies
│   └── idf_component.yml        # Component manager dependencies
├── sdkconfig.defaults           # Default configuration
├── README.md                    # This file
└── build/                       # Build output directory
```

### Configuration Options
Key TinyUSB settings in `sdkconfig.defaults`:
- `CONFIG_TINYUSB_HID_COUNT=1`: Enable one HID interface
- `CONFIG_TINYUSB_CDC_COUNT=0`: Disable CDC (not needed)
- `CONFIG_TINYUSB_MSC_ENABLED=n`: Disable MSC (not needed)

## Debugging

### UART Debug Output
The firmware logs all received UART data in hex format:
```
I (xxxx) esp32_keyboard: Received 3 bytes via UART: 1b 5b 41
```
This shows an arrow up sequence (ESC [ A).

### Monitor Mode
Run with verbose logging to see character processing:
```bash
idf.py monitor -b 115200
```

## Troubleshooting

### Common Issues

1. **Build Errors**:
   - Ensure ESP-IDF environment is properly configured
   - Check that ESP32-S3 target is selected

2. **USB Not Recognized**:
   - Try a different USB cable
   - Check USB port on computer
   - Ensure ESP32-S3 has native USB support

3. **Arrow Keys Not Working**:
   - Verify your terminal sends proper escape sequences
   - Check UART debug output for received bytes
   - Try using standard arrow keys in terminal

4. **Character Mapping Issues**:
   - Check debug output for received hex values
   - Verify character encoding (should be ASCII/UTF-8)

### Logs and Debug Information
Enable detailed logging by checking the UART monitor output. The firmware logs:
- UART data reception in hex format
- Character processing status
- USB HID report transmission

## Project Structure Details

### Main Components
- **UART Driver**: Handles serial communication with configurable buffer sizes
- **TinyUSB Integration**: Uses ESP-specific wrapper for USB HID functionality
- **FreeRTOS Tasks**: Dedicated task for UART event processing
- **Character Processing**: Comprehensive mapping from terminal input to USB HID output

### Dependencies
- ESP-IDF 6.0+
- ESP TinyUSB Component
- ESP32-S3 Hardware with native USB

## Contributing

Feel free to fork and improve the project! Areas for enhancement:
- Support for additional keyboard layouts
- Mouse functionality
- Custom key bindings
- Wireless communication (Bluetooth/WiFi)

## License

This project is provided as-is for educational and development purposes. The implementation is based on ESP-IDF examples and TinyUSB documentation.

## Credits

Based on ESP-IDF TinyUSB examples and enhanced for UART-to-USB keyboard functionality. Supports the ESP32-S3-USB-OTG development board and other ESP32-S3 variants with native USB support.