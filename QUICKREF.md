# Bottle VM - Quick Reference

## Common Commands

### Testing

```bash
# Run unit tests
cd test_vm
./build_tests.bat

# Run integration test
cd test_vm
./build.bat

# View test results summary
./unit_tests.exe 2>&1 | grep -E "^(===|PASS|FAIL|TEST SUMMARY|Passed|Failed|Total)"
```

### Building

```bash
# Build for ESP32
pio run

# Upload to ESP32
pio run --target upload

# Clean build
pio run --target clean

# Monitor serial output
pio device monitor
```

### Development

```bash
# Create new module
mkdir modules_dev/my_module
# Edit modules_dev/my_module/main.bottle

# Test module natively
cd test_vm
# Edit minimal_test.cpp to load your module
./build.bat
```

## File Locations

| Item | Path |
|------|------|
| Design docs | `CLAUDE.md` |
| Project config | `PROJECT_CONFIG.md` |
| Test status | `TEST_STATUS.md` |
| Unit tests | `test_vm/unit_tests.cpp` |
| Integration test | `test_vm/minimal_test.cpp` |
| Example module | `modules_dev/rhythm_spectrum/main.bottle` |
| Compiler | `D:\llvm-mingw-20260421-msvcrt-x86_64\bin\clang++.exe` |

## Key Constants

```cpp
// In include/bottle_vm.h
#define BOTTLE_MAX_STACK 128
#define BOTTLE_MAX_INSTRUCTIONS_PER_FRAME 20000

// In include/bottle_compiler.h
#define BOTTLE_MAX_BYTECODE 2048
#define BOTTLE_MAX_CONSTANTS 128
#define BOTTLE_MAX_ARRAYS 6
#define BOTTLE_MAX_SCALARS 12

// In include/common.h
#define MATRIX_WIDTH 17
#define MATRIX_HEIGHT 8
```

## Critical Implementation Details

### Jump Instructions
- **Always use signed relative offsets**: `vm->pc += (int16_t)offset`
- Compiler: `patch_jump()` writes absolute address
- VM: Converts to relative by subtracting current PC

### Memory Allocation
- **Always heap-allocate** `bottle_program_t` and `bottle_vm_t`
- These structures are 5KB+ and will overflow stack

### Array Bounds
- **Always use** `program->arrays[idx].length`
- **Never use** hardcoded `MATRIX_WIDTH` for bounds checking

### Stack Management
- Check `stack_top == 0` after each frame
- Stack leaks indicate jump/cleanup issues

## Debugging Tips

### Enable Verbose Logging
```cpp
// In test files, uncomment:
// #define VERBOSE 1
```

### Check Bytecode Integrity
```cpp
// Calculate checksum
uint32_t checksum = 0;
for (int i = 0; i < program->bytecode_size; i++) {
    checksum += program->bytecode[i];
}
printf("Checksum: 0x%08X\n", checksum);
```

### Trace Stack Depth
```cpp
// After each instruction
printf("[STACK] PC=%d op=0x%02X: %d->%d\n", 
       pc, opcode, old_depth, vm->stack_top);
```

## Serial Commands (ESP32)

```
list                    # List available modules
load <name>             # Load and run module
unload                  # Unload current module
reload                  # Reload current module
config <key> <value>    # Set config value
status                  # Show system status
```

## Bottle Language Cheat Sheet

### Variable Declaration
```bottle
runtime bottle-vm@0.2
module category.name

state array[17] = 0     # uint8_t array
state scalar = 0.0      # float scalar

config option {
  type select
  label "Option"
  default 0
  options "A|B|C"
}

frame_ms 33
```

### Control Flow
```bottle
if condition {
    // code
} else if condition {
    // code
} else {
    // code
}

for x in array {
    array[x] = value
}

for i in range(0, 10) {
    // code
}
```

### Temporal Statements
```bottle
// Execute every 90ms
value = value + 1 every 90ms
```

### Built-in Functions
```bottle
// Math
max(a, b)
min(a, b)
clamp(v, lo, hi)
abs(x)
sqrt(x)
sin(x), cos(x)
random(min, max)

// Color
hsv(h, s, v)
rgb(r, g, b)
blend(c1, c2, amount)

// Hardware
read(SPECTRUM)
read(ACCEL)
clear(LEDS)
LEDS[x, y] = color
show(LEDS)
millis()
```

## Troubleshooting

| Error | Cause | Solution |
|-------|-------|----------|
| Stack leak | Jump cleanup missing | Check jump offsets, ensure OP_POP in both branches |
| Bytecode corruption | Stack overflow | Heap-allocate program and vm |
| Array bounds error | Wrong length check | Use `program->arrays[idx].length` |
| Instruction limit | Complex nested loops | Increase `BOTTLE_MAX_INSTRUCTIONS_PER_FRAME` |
| Unknown opcode | Jump to wrong address | Verify jump semantics (relative vs absolute) |

## Test Results Quick Check

```bash
# Should show 14 passed, 4 failed
cd test_vm
./unit_tests.exe 2>&1 | tail -10
```

Expected output:
```
==========================================================
TEST SUMMARY
==========================================================
Passed: 14
Failed: 4
Total:  18
==========================================================
```

## Documentation Index

1. **CLAUDE.md** - Complete architecture and design
2. **README.md** - Project overview and quick start
3. **PROJECT_CONFIG.md** - Paths and configuration
4. **TEST_STATUS.md** - Current test results
5. **SUMMARY.md** - Organization summary
6. **QUICKREF.md** - This file
7. **test_vm/README.md** - Test suite details

## Memory System

Project knowledge saved in `.claude/memory/`:
- `project_compiler_path.md` - Compiler location
- `MEMORY.md` - Memory index

## Version Info

**VM Version:** 0.2  
**Last Updated:** 2026-05-05  
**Test Pass Rate:** 78% (14/18)  
**Integration Test:** ✓ Passing  
**ESP32 Ready:** Yes
