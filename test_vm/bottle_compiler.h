#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "bottle_types.h"
#include "bottle_opcodes.h"
#include "bottle_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compilation limits
#define BOTTLE_MAX_BYTECODE 2048
#define BOTTLE_MAX_CONSTANTS 128
#define BOTTLE_MAX_STRING_POOL 512
#define BOTTLE_MAX_DEBUG_INFO 512
#define BOTTLE_MAX_ARRAYS 6
#define BOTTLE_MAX_SCALARS 12
#define BOTTLE_MAX_CONFIGS 4
#define BOTTLE_NAME_LEN 16

// Array declaration
typedef struct {
  char name[BOTTLE_NAME_LEN];
  uint8_t length;
  bottle_value_type_t element_type;  // BOTTLE_TYPE_INT or BOTTLE_TYPE_FLOAT
  union {
    uint8_t int_value;    // For integer arrays
    float float_value;    // For float arrays
  } initial_value;
} bottle_array_def_t;

// Scalar declaration
typedef struct {
  char name[BOTTLE_NAME_LEN];
  float initial_value;
} bottle_scalar_def_t;

// Config declaration
typedef struct {
  char key[BOTTLE_NAME_LEN];
  char label[32];
  uint8_t type; // 1=select
  int16_t min_value;
  int16_t max_value;
  int16_t default_value;
  char options[96];
} bottle_config_def_t;

// Compiled program
typedef struct {
  // Bytecode
  uint8_t bytecode[BOTTLE_MAX_BYTECODE];
  uint16_t bytecode_size;

  // Constant pool
  bottle_value_t constants[BOTTLE_MAX_CONSTANTS];
  uint16_t constant_count;

  // String pool (for variable names, debug)
  char string_pool[BOTTLE_MAX_STRING_POOL];
  uint16_t string_pool_size;

  // Debug info (line numbers for each bytecode offset)
  uint16_t debug_info[BOTTLE_MAX_DEBUG_INFO];

  // Metadata
  bottle_array_def_t arrays[BOTTLE_MAX_ARRAYS];
  uint8_t array_count;
  bottle_scalar_def_t scalars[BOTTLE_MAX_SCALARS];
  uint8_t scalar_count;
  bottle_config_def_t configs[BOTTLE_MAX_CONFIGS];
  uint8_t config_count;

  // Entry points (bytecode offsets)
  uint16_t setup_offset;
  uint16_t loop_offset;
  uint16_t unload_offset;
  bool has_setup;
  bool has_loop;
  bool has_unload;

  // Frame timing
  uint16_t frame_ms;

  // Compilation result
  bottle_error_t error;
} bottle_program_t;

// Compile script text into bytecode program
bool bottle_compile(const char* script_text, bottle_program_t* out_program);

// Get human-readable opcode name (for debugging)
const char* bottle_opcode_name(bottle_opcode_t op);

#ifdef __cplusplus
}
#endif
