# Bottle VM Test Suite

This directory contains native test harnesses for the Bottle VM that run on the development machine (Windows/Linux/Mac) rather than on the ESP32 target.

## Structure

- **minimal_test.cpp** - Integration test that loads and executes a complete Bottle module
- **unit_tests.cpp** - Comprehensive unit test suite covering all VM features
- **Arduino.h** - Mock Arduino API for native compilation
- **bottle_*.h/cpp** - Symlinks/copies of the main VM source files
- **build.bat** - Build script for minimal_test using llvm-mingw
- **build_tests.bat** - Build script for unit_tests using llvm-mingw

## Building

### Prerequisites

- **llvm-mingw** compiler installed at: `D:\llvm-mingw-20260421-msvcrt-x86_64\`
- Source files from `../src/` and `../include/`

### Build Commands

```bash
# Build integration test
cd test_vm
./build.bat

# Build unit tests
./build_tests.bat
```

## Running Tests

```bash
# Run integration test (executes rhythm_spectrum module 10 times)
./minimal_test.exe

# Run unit tests
./unit_tests.exe
```

## Test Coverage

### Unit Tests (`unit_tests.cpp`)

- **Compiler Tests**: Variable declarations, expressions, control flow, error handling
- **VM Execution Tests**: Stack operations, arithmetic, comparisons, jumps
- **Type System Tests**: Type conversions, color operations, boolean logic
- **Array Tests**: Read/write, boundary checks, loop access
- **Control Flow Tests**: if/else, while loops, for loops
- **Built-in Functions**: LED operations, sensors, config, math functions
- **Error Handling**: Syntax errors, runtime errors, stack overflow
- **Integration Tests**: Complete scripts, multiple entry points

### Integration Test (`minimal_test.cpp`)

- Loads a real Bottle module from `modules_dev/rhythm_spectrum/main.bottle`
- Executes setup → loop (10 iterations) → unload
- Validates:
  - No stack leaks (stack_top returns to 0 after each frame)
  - Bytecode integrity (checksum remains constant)
  - No memory corruption
  - Instruction count within limits

## Debugging

Both test programs include extensive logging:

- Stack depth tracking
- Bytecode checksums
- Memory addresses
- Array write operations
- Opcode execution traces

Set `VERBOSE=1` in the source to enable detailed logs.

## Known Issues

- **Path Dependencies**: Test programs expect module files in `../modules_dev/`
- **Arduino Mocks**: Not all Arduino functions are mocked; add to `Arduino.h` as needed
- **Heap Allocation**: Large structures (program, vm) must be heap-allocated to avoid stack overflow

## Architecture Notes

See [../CLAUDE.md](../CLAUDE.md) for complete VM architecture documentation.

### Key Fixes Applied

1. **Jump Instructions**: Use signed relative offsets, not absolute addresses
2. **Memory Allocation**: Heap-allocate `bottle_program_t` and `bottle_vm_t` (5KB+ structures)
3. **Array Bounds**: Use actual array length from `program->arrays[i].length`, not hardcoded `MATRIX_WIDTH`
4. **Instruction Limits**: Raised to 20000 per frame for complex nested loops

### Test Strategy

1. **Unit tests** verify individual opcodes and compiler phases
2. **Integration tests** validate complete module execution
3. **Regression tests** ensure fixes don't break existing functionality
4. **Performance tests** measure instruction count and memory usage
