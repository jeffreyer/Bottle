# Bottle - LED Matrix Animation VM

Bottle is a bytecode virtual machine designed for creating dynamic LED matrix animations on ESP32 microcontrollers. It provides a simple scripting language that compiles to efficient bytecode, enabling real-time audio-reactive and sensor-driven visual effects.

## Features

- **Compact Bytecode VM**: Stack-based interpreter optimized for embedded systems
- **Type System**: Automatic conversions between int, float, bool, and color types
- **Built-in Functions**: Math, color manipulation, LED control, sensor access
- **Safety**: Stack overflow protection, array bounds checking, instruction limits
- **Audio Reactive**: FFT spectrum analysis integration
- **Sensor Integration**: Accelerometer, gyroscope, orientation detection
- **Modular Design**: Hot-swappable animation modules with setup/loop/unload lifecycle

## Hardware

- **Target**: ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1-N4R2)
- **Memory**: 4MB Flash, 2MB PSRAM
- **LED Driver**: FastLED library
- **Audio**: I2S microphone with arduinoFFT

## Quick Start

### Prerequisites

- PlatformIO IDE or CLI
- ESP32-S3 development board
- LED matrix (WS2812B or compatible)

### Build and Upload

```bash
# Clone repository
git clone <repo-url>
cd Bottle

# Build and upload to ESP32
pio run -t upload

# Monitor serial output
pio device monitor
```

### Write Your First Module

Create `modules_dev/my_animation/main.bottle`:

```bottle
array colors[16] = 0
scalar hue = 0

setup() {
    clear_leds()
}

loop(50) {
    for (x = 0; x < 16; x = x + 1) {
        colors[x] = hsv(hue + x * 10, 255, 255)
        set_led(x, colors[x])
    }
    show_leds()
    hue = (hue + 1) % 360
}
```

Load via serial commands:
```
load my_animation
```

## Architecture

See [CLAUDE.md](CLAUDE.md) for detailed architecture documentation.

### Components

- **Compiler** (`bottle_compiler.cpp`): Single-pass compiler, constant folding, loop unrolling
- **VM** (`bottle_vm.cpp`): Stack-based interpreter with 42 opcodes
- **Type System** (`bottle_types.cpp`): Tagged union values with automatic coercion
- **Runtime** (`module_runtime.cpp`): Module lifecycle management
- **Registry** (`module_registry.cpp`): Module storage and discovery

### Memory Limits

- Stack depth: 128 values
- Bytecode size: 2048 bytes
- Constants: 128 max
- Arrays: 6 × 16 elements (uint8_t)
- Scalars: 12 (float)
- Configs: 4 (int32_t)
- Instructions per frame: 20,000 max

## Language Reference

### Variable Declarations

```bottle
array peaks[16] = 0          // uint8_t array
scalar brightness = 1.0      // float scalar
config speed = 100           // int32_t config (persistent)
```

### Lifecycle Functions

```bottle
setup() {
    // Run once on module load
}

loop(50) {
    // Run every 50ms
}

unload() {
    // Run once on module unload
}
```

### Built-in Functions

**Math**: `sin(x)`, `cos(x)`, `abs(x)`, `min(a,b)`, `max(a,b)`, `clamp(x,lo,hi)`, `lerp(a,b,t)`, `map(x,in_min,in_max,out_min,out_max)`

**Color**: `hsv(h,s,v)`, `blend(c1,c2,ratio)`

**LED**: `clear_leds()`, `set_led(index, color)`, `show_leds()`

**Sensors**: `read_spectrum(index)`, `read_gravity()`, `read_orientation()`

**Config**: `get_config(index)`, `set_config(index, value)`

### Operators

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Logical: `&&`, `||`, `!`

### Control Flow

```bottle
if (condition) {
    // true branch
} else {
    // false branch
}

while (condition) {
    // loop body
}

for (i = 0; i < 10; i = i + 1) {
    // loop body
}
```

### Temporal Statements

```bottle
// Execute every 90ms
peaks[x] = max(peaks[x] - 1, spectrum[x]) every 90ms
```

## Testing

See [test_vm/README.md](test_vm/README.md) for native testing documentation.

```bash
# Build and run unit tests
cd test_vm
./build_tests.bat
./unit_tests.exe

# Build and run integration tests
./build.bat
./minimal_test.exe
```

## Project Structure

```
Bottle/
├── src/                    # Main source code
│   ├── bottle_compiler.cpp # Compiler implementation
│   ├── bottle_vm.cpp       # VM interpreter
│   ├── bottle_types.cpp    # Type system
│   ├── module_runtime.cpp  # Module lifecycle
│   ├── module_registry.cpp # Module management
│   └── main.cpp            # ESP32 entry point
├── include/                # Header files
│   ├── bottle_*.h          # VM headers
│   ├── module_*.h          # Module system headers
│   └── app_control.h       # Application control
├── test_vm/                # Native test suite
│   ├── unit_tests.cpp      # Unit tests
│   ├── minimal_test.cpp    # Integration tests
│   └── README.md           # Test documentation
├── modules_dev/            # Development modules
│   └── rhythm_spectrum/    # Example audio-reactive module
├── docs/                   # Additional documentation
├── CLAUDE.md               # Architecture documentation
├── PROJECT_CONFIG.md       # Build configuration
└── platformio.ini          # PlatformIO configuration
```

## Serial Commands

Connect via serial (115200 baud):

```
list                        # List available modules
load <module_name>          # Load and run a module
unload                      # Unload current module
reload                      # Reload current module
config <key> <value>        # Set configuration value
status                      # Show system status
```

## Development Workflow

1. **Write** module in `modules_dev/<name>/main.bottle`
2. **Test** natively: `cd test_vm && ./build.bat && ./minimal_test.exe`
3. **Upload** to ESP32: `pio run -t upload`
4. **Load** via serial: `load <name>`
5. **Debug** with serial monitor: `pio device monitor`

## Troubleshooting

### Stack Overflow
- Reduce loop nesting depth
- Simplify expressions (fewer intermediate values)
- Check for infinite loops

### Instruction Limit Exceeded
- Reduce loop iterations
- Simplify calculations
- Split complex logic across multiple frames

### Array Bounds Error
- Verify array indices are within [0, length-1]
- Check loop bounds match array size

### Unknown Opcode
- Recompile module (may be corrupted bytecode)
- Check for compiler errors in serial output

## Contributing

1. Fork the repository
2. Create a feature branch
3. Add tests for new features
4. Ensure all tests pass
5. Submit a pull request

## License

[Specify license here]

## Credits

- **FastLED**: LED control library
- **arduinoFFT**: FFT implementation
- **ESP32 Arduino Core**: ESP32 framework

## Contact

[Your contact information]
