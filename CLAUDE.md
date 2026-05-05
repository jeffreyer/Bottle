# Bottle VM - Design Documentation

## Project Overview

**Bottle VM** is a custom bytecode virtual machine designed for ESP32-based LED matrix devices. It enables users to write high-level scripts in the Bottle language to create dynamic LED animations and effects that respond to audio spectrum data and gravity sensors.

### Key Features
- Custom scripting language with C-like syntax
- Real-time audio spectrum visualization
- Gravity-based orientation detection
- HSV/RGB color manipulation
- Temporal statements with `every` keyword for time-based updates
- User-configurable parameters via config system

### Hardware Target
- **Platform**: ESP32-S3 (N4R2)
- **LED Matrix**: 17x8 (WIDTH=17, HEIGHT=8)
- **Sensors**: Audio spectrum analyzer, accelerometer
- **Framework**: Arduino + PlatformIO

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      User Script (.bottle)                   │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   Bottle Compiler                            │
│  - Tokenizer/Lexer                                           │
│  - Parser (recursive descent)                                │
│  - Bytecode generator                                        │
│  - Constant pool management                                  │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   Bottle Program                             │
│  - Bytecode array                                            │
│  - Constant pool                                             │
│  - Metadata (arrays, scalars, configs)                       │
│  - Entry points (setup, loop, unload)                        │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                     Bottle VM                                │
│  - Stack-based execution engine                              │
│  - State management (arrays, scalars)                        │
│  - Hardware API integration                                  │
│  - Error handling                                            │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   Hardware APIs                              │
│  - LED control (FastLED)                                     │
│  - Spectrum analyzer (FFT)                                   │
│  - Accelerometer (gravity)                                   │
└─────────────────────────────────────────────────────────────┘
```

---

## Bottle Language Syntax

### Script Structure

```bottle
runtime bottle-vm@0.2
module <category>.<name>

// State declarations
state <name>[SIZE] = <initial_value>  // Array
state <name> = <initial_value>        // Scalar

// Configuration
config <key> {
  type select
  label "Display Name"
  default <value>
  options "Option1|Option2|Option3"
}

frame_ms <milliseconds>

setup {
  // Initialization code (runs once)
}

loop {
  // Main loop (runs every frame)
}

unload {
  // Cleanup code (runs on module exit)
}
```

### Data Types

- **int**: 32-bit signed integer
- **float**: 32-bit floating point
- **bool**: true/false
- **color**: RGB color (from `rgb()` or `hsv()`)
- **array**: Fixed-size uint8_t array (for LED values)

### Operators

**Arithmetic**: `+`, `-`, `*`, `/`, `%`, `-` (unary)
**Comparison**: `<`, `<=`, `>`, `>=`, `==`, `!=`
**Logical**: `&&`, `||`, `!`

### Control Flow

```bottle
// If statement
if <condition> {
  // code
} else if <condition> {
  // code
} else {
  // code
}

// For loop (array iteration)
for <var> in <array> {
  // var is the index (0 to length-1)
  // Access element: array[var]
}

// For loop (range)
for <var> in range(<start>, <end>) {
  // var goes from start to end-1
}
```

### Temporal Statements

Execute code periodically using the `every` keyword:

```bottle
// Update every 90ms
peaks[x] = max(peaks[x] - 1, spectrum[x]) every 90ms

// Increment phase every 33ms
phase = phase + 0.1 every 33ms
```

**Implementation**: Compiler generates bytecode to check elapsed time since last execution.

### Built-in Functions

**Math**:
- `max(a, b)` - Maximum of two values
- `min(a, b)` - Minimum of two values
- `clamp(v, lo, hi)` - Constrain value to range
- `abs(x)` - Absolute value
- `sqrt(x)` - Square root
- `sin(x)`, `cos(x)` - Trigonometric (radians)
- `random(min, max)` - Random integer

**Color**:
- `hsv(h, s, v)` - Create color from HSV (h: 0-255, s: 0-255, v: 0-255)
- `rgb(r, g, b)` - Create color from RGB (0-255 each)
- `blend(a, b, amount)` - Blend two colors (amount: 0-255)

**Hardware**:
- `read(SPECTRUM)` - Read audio spectrum into array
- `read(ACCEL)` - Update orientation from gravity
- `clear(LEDS)` - Clear LED matrix
- `show(LEDS)` - Flush LED updates to hardware
- `LEDS[x, y] = color` - Set LED at position

**Time**:
- `millis()` - Current time in milliseconds

### Constants

- `WIDTH` - Matrix width (17)
- `HEIGHT` - Matrix height (8)

---

## Bytecode Instruction Set

### Stack Operations
- `OP_PUSH_CONST [u16]` - Push constant from pool
- `OP_PUSH_SCALAR [u8]` - Push scalar variable
- `OP_PUSH_ARRAY [u8]` - Push array[index] (index on stack)
- `OP_POP_SCALAR [u8]` - Pop to scalar variable
- `OP_POP_ARRAY [u8]` - Pop to array[index] (value and index on stack)
- `OP_DUP` - Duplicate top of stack
- `OP_POP` - Discard top of stack

### Arithmetic
- `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD`, `OP_NEG`

### Comparison
- `OP_LT`, `OP_LE`, `OP_GT`, `OP_GE`, `OP_EQ`, `OP_NE`

### Logical
- `OP_AND`, `OP_OR`, `OP_NOT`

### Control Flow
- `OP_JUMP [u16]` - Unconditional jump (signed relative offset)
- `OP_JUMP_IF_FALSE [u16]` - Jump if top is false (signed relative offset)
- `OP_JUMP_IF_TRUE [u16]` - Jump if top is true (signed relative offset)

**CRITICAL**: All jumps use **signed relative offsets** (int16_t). The VM adds the offset to the current PC:
```cpp
int16_t offset = (int16_t)read_uint16(program, &vm->pc);
vm->pc += offset;
```

### Built-in Calls
- `OP_CALL_MAX`, `OP_CALL_MIN`, `OP_CALL_CLAMP`, `OP_CALL_ABS`
- `OP_CALL_SQRT`, `OP_CALL_SIN`, `OP_CALL_COS`, `OP_CALL_RANDOM`
- `OP_CALL_MILLIS`
- `OP_CALL_HSV`, `OP_CALL_RGB`, `OP_CALL_BLEND`

### Hardware APIs
- `OP_READ_SPECTRUM [u8]` - Read spectrum to array
- `OP_READ_ACCEL` - Update orientation
- `OP_CLEAR_LEDS`, `OP_SET_LED`, `OP_SHOW_LEDS`

### Special
- `OP_HALT` (0xFF) - End of program

---

## Compiler Implementation

### File: `src/bottle_compiler.cpp`

#### Key Functions

**`bottle_compile(script_text, out_program)`**
- Entry point for compilation
- Initializes tokenizer and compiler state
- Parses declarations and blocks
- Returns compiled program

**`patch_jump(compiler, offset)`**
- Patches forward jump instructions
- Writes **absolute target address** to bytecode
- Used for if/else and temporal statements

**`for_statement(compiler)`**
- Generates loop bytecode
- Creates loop iterator scalar
- Emits initialization, condition check, body, increment
- Backward jump uses **signed relative offset** via `emit_u16(loop_start)`

**`temporal_statement(compiler)`**
- Handles `every` keyword
- Generates time check: `(millis() - last_tick) >= interval`
- Emits conditional jump to skip body if not ready
- Updates last_tick scalar after execution

#### Jump Semantics

**Forward jumps** (if/else, temporal):
1. `emit_jump()` emits opcode and placeholder offset
2. `patch_jump()` writes absolute target address

**Backward jumps** (for loops):
1. Save `loop_start = current_offset()`
2. After loop body, emit `OP_JUMP` + `emit_u16(loop_start)`
3. Offset is calculated as: `loop_start - current_position` (negative)

**VM execution**:
```cpp
case OP_JUMP: {
  int16_t offset = (int16_t)read_uint16(program, &vm->pc);
  vm->pc += offset;  // Relative jump
  break;
}
```

---

## VM Implementation

### File: `src/bottle_vm.cpp`

#### Key Data Structures

**`bottle_vm_t`**
- `arrays[BOTTLE_MAX_ARRAYS][MATRIX_WIDTH]` - State arrays (uint8_t)
- `scalars[BOTTLE_MAX_SCALARS]` - State scalars (float)
- `stack[BOTTLE_MAX_STACK]` - Evaluation stack (bottle_value_t)
- `stack_top` - Current stack depth
- `pc` - Program counter
- `instruction_count` - Instruction limit counter
- `error` - Error state

#### Key Functions

**`bottle_vm_init(vm, program)`**
- Initializes VM state from program metadata
- Copies initial values for arrays and scalars
- Resets stack and PC

**`bottle_vm_execute(vm, program, entry_offset, ctx)`**
- Main execution loop
- Fetches and decodes instructions
- Enforces instruction limit (20000 per frame)
- Handles errors and halts

**`push(vm, value)` / `pop(vm)`**
- Stack manipulation with bounds checking
- Critical for preventing stack overflow/underflow

#### Array Bounds Checking

**CRITICAL**: Always use actual array length, not hardcoded constants:

```cpp
case OP_PUSH_ARRAY: {
  uint8_t array_idx = read_byte(program, &vm->pc);
  bottle_value_t index_val = pop(vm);
  int32_t index = bottle_to_int(index_val);
  
  // Use program->arrays[array_idx].length, NOT MATRIX_WIDTH
  uint8_t array_length = program->arrays[array_idx].length;
  if (index < 0 || index >= array_length) {
    bottle_error_set(&vm->error, program->debug_info[vm->pc], 0,
                    "Array index out of bounds: %d", index);
    return;
  }
  
  push(vm, bottle_int(vm->arrays[array_idx][index]));
  break;
}
```

---

## Common Pitfalls and Solutions

### 1. Stack Leaks

**Symptom**: `stack_top` increases after each frame, eventually overflows.

**Cause**: Jump instructions skip cleanup code (e.g., `OP_POP` after conditionals).

**Solution**: Ensure jump semantics are consistent between compiler and VM. Use signed relative offsets for all jumps.

### 2. Bytecode Corruption

**Symptom**: Bytecode values change during execution (e.g., `0x07` becomes `0x7F`).

**Cause**: Stack overflow writing into program memory when both are allocated on the stack.

**Solution**: Allocate large structures (`bottle_program_t`, `bottle_vm_t`) on the heap using `malloc()`.

### 3. Array Index Out of Bounds

**Symptom**: "Array index out of bounds: 17" error.

**Cause**: Using hardcoded `MATRIX_WIDTH` instead of actual array length.

**Solution**: Always use `program->arrays[array_idx].length` for bounds checking.

### 4. Instruction Limit Exceeded

**Symptom**: "Instruction limit exceeded (infinite loop?)" error.

**Cause**: Complex scripts with nested loops exceed the instruction limit.

**Solution**: Increase `BOTTLE_MAX_INSTRUCTIONS_PER_FRAME` in `bottle_vm.h` (currently 20000).

### 5. Jump Target Mismatch

**Symptom**: "Unknown opcode" errors, jumps landing at wrong locations.

**Cause**: Compiler generates relative offsets but VM executes as absolute, or vice versa.

**Solution**: Ensure consistent jump semantics:
- Compiler: `patch_jump()` writes absolute address OR relative offset
- VM: Executes as `vm->pc += offset` (relative) OR `vm->pc = offset` (absolute)
- **Current implementation uses relative offsets**

### 6. Temporal Statement Stack Issues

**Symptom**: Stack grows when temporal statements execute.

**Cause**: Both true and false branches must clean up the same stack items (condition, value, index).

**Solution**: Ensure `OP_POP` instructions are emitted in both branches of the conditional jump.

---

## Testing Strategy

### Test Environment

**Location**: `test_vm/`

**Build System**: `build.bat` using llvm-mingw compiler

**Compiler Path**: `D:\llvm-mingw-20260421-msvcrt-x86_64\bin\clang++.exe`

### Test Files

- `minimal_test.cpp` - Main VM test with stack tracing
- `Arduino.h` - Mock Arduino functions for native testing
- `bottle_builtins_mock.cpp` - Mock hardware APIs
- Local copies of headers (must be kept in sync with `include/`)

### Running Tests

```bash
cd test_vm
./build.bat
./minimal_test.exe
```

### Test Script

**Location**: `modules_dev/rhythm_spectrum/main.bottle`

**Features tested**:
- Array operations
- Nested loops (17x17 iterations)
- Temporal statements with `every`
- Conditional logic (if/else if chains)
- Math functions (max, min, clamp)
- Color functions (hsv, rgb)
- Hardware APIs (spectrum, LEDs)

### Verification Checklist

- [ ] All 10 frames complete successfully
- [ ] `stack_top=0` after each frame
- [ ] Bytecode checksum remains stable
- [ ] No "Unknown opcode" errors
- [ ] No array bounds errors
- [ ] Instruction count under limit

---

## Build and Deployment

### PlatformIO Configuration

**File**: `platformio.ini`

```ini
[env:pico32]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38-1/platform-espressif32.zip
board = esp32s3dev_n4r2
framework = arduino
monitor_speed = 115200
board_build.partitions = partitions.csv
lib_deps = 
  fastled/FastLED@^3.10.3
  kosme/arduinoFFT@^2.0.4
```

### Build Commands

```bash
# Build for ESP32
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

### Memory Considerations

- **Heap allocation**: Use `malloc()` for `bottle_program_t` and `bottle_vm_t` to avoid stack overflow
- **Bytecode size**: Max 2048 bytes per program
- **Stack depth**: Max 128 values
- **Instruction limit**: 20000 per frame (adjustable)

---

## Module System Integration

### Module Runtime Context

**File**: `include/module_runtime.h`

Provides hardware API access to VM:

```cpp
struct module_context_t {
  module_sensor_api_t sensor;    // spectrum[], gravity
  module_led_api_t led;          // clear(), set(), show()
  module_config_api_t config;    // get_int(), set_int()
  uint32_t now_ms;               // Current time
  void* state;                   // Module-specific state
};
```

### Module Lifecycle

1. **Load**: Compile script, initialize VM
2. **Setup**: Execute `setup` block once
3. **Loop**: Execute `loop` block every frame (33ms typical)
4. **Unload**: Execute `unload` block, cleanup

---

## Development Workflow

### Adding New Features

1. **Update opcodes** in `include/bottle_opcodes.h`
2. **Add compiler support** in `src/bottle_compiler.cpp`
3. **Implement VM execution** in `src/bottle_vm.cpp`
4. **Add built-in function** in `src/bottle_builtins.cpp` (if needed)
5. **Write test case** in `test_vm/`
6. **Update this documentation**

### Debugging Tips

1. **Enable verbose logging** in compiler and VM
2. **Add stack tracing** to track stack depth changes
3. **Disassemble bytecode** to verify instruction sequence
4. **Use checksums** to detect memory corruption
5. **Test in simulator** before deploying to hardware

### Code Style

- Use C-style comments for implementation notes
- Keep functions focused and single-purpose
- Validate inputs at API boundaries
- Use descriptive variable names
- Add error messages with context

---

## File Structure

```
Bottle/
├── include/
│   ├── bottle_compiler.h      # Compiler API
│   ├── bottle_vm.h            # VM API
│   ├── bottle_opcodes.h       # Instruction set
│   ├── bottle_types.h         # Value types
│   ├── bottle_error.h         # Error handling
│   ├── module_runtime.h       # Hardware API context
│   └── module_*.h             # Module implementations
├── src/
│   ├── bottle_compiler.cpp    # Compiler implementation
│   ├── bottle_vm.cpp          # VM implementation
│   ├── bottle_builtins.cpp    # Built-in functions
│   ├── bottle_error.cpp       # Error utilities
│   └── module_*.cpp           # Module implementations
├── test_vm/
│   ├── build.bat              # Native build script
│   ├── minimal_test.cpp       # VM test harness
│   ├── Arduino.h              # Arduino mocks
│   └── bottle_*.h             # Local header copies
├── modules_dev/
│   └── rhythm_spectrum/
│       └── main.bottle        # Test script
├── platformio.ini             # ESP32 build config
└── CLAUDE.md                  # This file
```

---

## Known Issues and TODO

### Current Issues
- None (all major bugs resolved as of last test)

### Future Enhancements
- [ ] Add `while` loop support
- [ ] Add function definitions
- [ ] Add string type and operations
- [ ] Optimize bytecode size (use variable-length encoding)
- [ ] Add breakpoint/step debugging
- [ ] Support multiple LED matrices
- [ ] Add touch input API
- [ ] Implement module hot-reload

---

## Version History

**v0.2** (Current)
- Fixed stack leak in temporal statements
- Fixed jump instruction semantics (relative offsets)
- Fixed array bounds checking
- Increased instruction limit to 20000
- Added comprehensive test suite

**v0.1** (Initial)
- Basic compiler and VM
- Core instruction set
- Hardware API integration
- Module system

---

## Contact and Support

For issues and feature requests, refer to the project repository or contact the development team.

---

*Last updated: 2026-05-XX*
