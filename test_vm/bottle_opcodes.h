#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bytecode instruction set for Bottle VM
typedef enum {
  // Stack operations
  OP_PUSH_CONST = 0x01,      // Push constant from pool [u16 index]
  OP_PUSH_SCALAR = 0x02,     // Push scalar variable [u8 index]
  OP_PUSH_ARRAY = 0x03,      // Push array[index] [u8 array_index] (index on stack)
  OP_POP_SCALAR = 0x04,      // Pop to scalar variable [u8 index]
  OP_POP_ARRAY = 0x05,       // Pop to array[index] [u8 array_index] (index on stack, value on stack)
  OP_DUP = 0x06,             // Duplicate top of stack
  OP_POP = 0x07,             // Discard top of stack

  // Arithmetic (float-aware)
  OP_ADD = 0x10,
  OP_SUB = 0x11,
  OP_MUL = 0x12,
  OP_DIV = 0x13,
  OP_MOD = 0x14,
  OP_NEG = 0x15,

  // Comparison (result is bool)
  OP_LT = 0x20,
  OP_LE = 0x21,
  OP_GT = 0x22,
  OP_GE = 0x23,
  OP_EQ = 0x24,
  OP_NE = 0x25,

  // Logical (bool operations)
  OP_AND = 0x30,
  OP_OR = 0x31,
  OP_NOT = 0x32,

  // Control flow
  OP_JUMP = 0x40,            // Unconditional jump [u16 offset]
  OP_JUMP_IF_FALSE = 0x41,   // Jump if top of stack is false [u16 offset]
  OP_JUMP_IF_TRUE = 0x42,    // Jump if top of stack is true [u16 offset]

  // Built-in math functions
  OP_CALL_MAX = 0x50,        // max(a, b)
  OP_CALL_MIN = 0x51,        // min(a, b)
  OP_CALL_CLAMP = 0x52,      // clamp(v, lo, hi)
  OP_CALL_ABS = 0x53,        // abs(x)
  OP_CALL_SQRT = 0x54,       // sqrt(x)
  OP_CALL_SIN = 0x55,        // sin(x) in radians
  OP_CALL_COS = 0x56,        // cos(x) in radians
  OP_CALL_RANDOM = 0x57,     // random(min, max)
  OP_CALL_MILLIS = 0x58,     // millis() - returns ctx->now_ms

  // Color functions
  OP_CALL_HSV = 0x60,        // hsv(h, s, v) -> color
  OP_CALL_RGB = 0x61,        // rgb(r, g, b) -> color
  OP_CALL_BLEND = 0x62,      // blend(a, b, amount) -> color

  // Hardware APIs
  OP_READ_SPECTRUM = 0x70,   // Read spectrum to array [u8 array_index]
  OP_READ_ACCEL = 0x71,      // Update orientation from gravity
  OP_CLEAR_LEDS = 0x72,      // Clear LED matrix
  OP_SET_LED = 0x73,         // Set LED at (x, y) to color (pops color, y, x)
  OP_SHOW_LEDS = 0x74,       // Flush LED updates

  // Debug
  OP_PRINT = 0x80,           // Print top of stack to Serial

  // Special
  OP_HALT = 0xFF,            // End of program
} bottle_opcode_t;

// Opcode argument sizes (for disassembly/debugging)
static inline uint8_t bottle_opcode_arg_size(bottle_opcode_t op) {
  switch (op) {
    case OP_PUSH_CONST:
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
      return 2; // u16 argument

    case OP_PUSH_SCALAR:
    case OP_PUSH_ARRAY:
    case OP_POP_SCALAR:
    case OP_POP_ARRAY:
    case OP_READ_SPECTRUM:
      return 1; // u8 argument

    default:
      return 0; // No arguments
  }
}

#ifdef __cplusplus
}
#endif
