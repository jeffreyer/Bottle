# Bottle VM - Project Configuration
# This file contains all paths and settings for the project

## Build Tools

### Native Compiler (for test_vm)
COMPILER_PATH=D:\llvm-mingw-20260421-msvcrt-x86_64\bin\clang++.exe

### PlatformIO (for ESP32)
# PlatformIO is expected to be in PATH
# If not, install via: pip install platformio

## Project Paths

### Source Code
PROJECT_ROOT=c:\Users\jeff\Desktop\Bottle
INCLUDE_DIR=c:\Users\jeff\Desktop\Bottle\include
SRC_DIR=c:\Users\jeff\Desktop\Bottle\src
TEST_DIR=c:\Users\jeff\Desktop\Bottle\test_vm
MODULES_DIR=c:\Users\jeff\Desktop\Bottle\modules_dev

### Test Scripts
TEST_SCRIPT=c:\Users\jeff\Desktop\Bottle\modules_dev\rhythm_spectrum\main.bottle

## Hardware Configuration

### LED Matrix
MATRIX_WIDTH=17
MATRIX_HEIGHT=8

### ESP32 Board
BOARD=esp32s3dev_n4r2
PLATFORM=espressif32
MONITOR_SPEED=115200

## Build Commands

### Native Test Build
# cd test_vm
# build.bat                    # Build and run minimal_test
# build_tests.bat              # Build and run unit tests

### ESP32 Build
# pio run                      # Build firmware
# pio run --target upload      # Upload to device
# pio device monitor           # Monitor serial output

### Python Serial Monitor
# python read_serial.py        # Read serial output (if exists)

## Testing

### Unit Test Executable
UNIT_TEST_EXE=c:\Users\jeff\Desktop\Bottle\test_vm\unit_tests.exe

### Integration Test Executable
INTEGRATION_TEST_EXE=c:\Users\jeff\Desktop\Bottle\test_vm\minimal_test.exe

## Dependencies

### PlatformIO Libraries
# FastLED@^3.10.3
# arduinoFFT@^2.0.4

## Notes

# 1. All paths use Windows-style backslashes or forward slashes
# 2. Compiler path must be updated if llvm-mingw is installed elsewhere
# 3. Test scripts are in test_vm/ directory
# 4. Module scripts are in modules_dev/ directory
# 5. Build artifacts are generated in .pio/ directory (gitignored)
