#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "bottle_types.h"
#include "bottle_compiler.h"
#include "module_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runtime limits
#define BOTTLE_MAX_STACK 128
#define BOTTLE_MAX_INSTRUCTIONS_PER_FRAME 20000

// VM runtime state
typedef struct {
  // State arrays (uint8_t for LED values)
  uint8_t arrays[BOTTLE_MAX_ARRAYS][MATRIX_WIDTH];

  // State scalars (float for smooth animations)
  float scalars[BOTTLE_MAX_SCALARS];

  // Expression evaluation stack
  bottle_value_t stack[BOTTLE_MAX_STACK];
  uint8_t stack_top;

  // Execution state
  uint16_t pc;                    // Program counter
  uint16_t instruction_count;     // For timeout protection
  uint32_t last_frame_ms;         // Frame timing

  // Orientation (from gravity sensor)
  uint8_t orientation;            // 0-3 for 4 rotations
  uint8_t band_count;             // Width after rotation
  uint8_t value_count;            // Height after rotation

  // Error state
  bottle_error_t error;

  // Loop iteration context (for unrolled loops)
  uint8_t loop_iterator;

} bottle_vm_t;

// Initialize VM state from compiled program
void bottle_vm_init(bottle_vm_t* vm, const bottle_program_t* program);

// Execute a section of the program (setup/loop/unload)
void bottle_vm_execute(bottle_vm_t* vm, const bottle_program_t* program,
                       uint16_t entry_offset, module_context_t* ctx);

// Run the loop section (called every frame)
void bottle_vm_run_loop(bottle_vm_t* vm, const bottle_program_t* program, module_context_t* ctx);

// Get debug status as JSON
const char* bottle_vm_status_json(const bottle_vm_t* vm);

#ifdef __cplusplus
}
#endif
