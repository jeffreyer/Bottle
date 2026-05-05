#include "bottle_vm.h"
#include "bottle_opcodes.h"
#include "bottle_error.h"
#include "common.h"
#include <string.h>
#include <Arduino.h>
#include <math.h>

// Forward declarations for builtins
extern bottle_value_t builtin_max(bottle_value_t a, bottle_value_t b);
extern bottle_value_t builtin_min(bottle_value_t a, bottle_value_t b);
extern bottle_value_t builtin_clamp(bottle_value_t v, bottle_value_t lo, bottle_value_t hi);
extern bottle_value_t builtin_abs(bottle_value_t x);
extern bottle_value_t builtin_sqrt(bottle_value_t x);
extern bottle_value_t builtin_sin(bottle_value_t x);
extern bottle_value_t builtin_cos(bottle_value_t x);
extern bottle_value_t builtin_random(bottle_value_t min_val, bottle_value_t max_val);
extern bottle_value_t builtin_millis(uint32_t now_ms);
extern bottle_value_t builtin_hsv(bottle_value_t h, bottle_value_t s, bottle_value_t v);
extern bottle_value_t builtin_rgb(bottle_value_t r, bottle_value_t g, bottle_value_t b);
extern bottle_value_t builtin_blend(bottle_value_t a, bottle_value_t b, bottle_value_t amount);
extern void builtin_print(bottle_value_t value);

// Stack operations
static inline void push(bottle_vm_t* vm, bottle_value_t value) {
  if (vm->stack_top >= BOTTLE_MAX_STACK) {
    Serial.printf("[VM ERROR] Stack overflow! stack_top=%d, max=%d\n", vm->stack_top, BOTTLE_MAX_STACK);
    bottle_error_set(&vm->error, 0, 0, "Stack overflow");
    return;
  }
  vm->stack[vm->stack_top++] = value;

  // Track maximum stack depth
  static uint8_t max_stack = 0;
  if (vm->stack_top > max_stack) {
    max_stack = vm->stack_top;
  }
}

static inline bottle_value_t pop(bottle_vm_t* vm) {
  if (vm->stack_top == 0) {
    bottle_error_set(&vm->error, 0, 0, "Stack underflow");
    return bottle_int(0);
  }
  return vm->stack[--vm->stack_top];
}

static inline bottle_value_t peek(bottle_vm_t* vm, uint8_t distance) {
  if (distance >= vm->stack_top) {
    return bottle_int(0);
  }
  return vm->stack[vm->stack_top - 1 - distance];
}

// Bytecode reading
static inline uint8_t read_byte(const bottle_program_t* program, uint16_t* pc) {
  return program->bytecode[(*pc)++];
}

static inline uint16_t read_u16(const bottle_program_t* program, uint16_t* pc) {
  uint16_t high = read_byte(program, pc);
  uint16_t low = read_byte(program, pc);
  return (high << 8) | low;
}

// Orientation mapping (from gravity sensor)
static void update_orientation(bottle_vm_t* vm, module_context_t* ctx) {
  float gx = ctx->sensor.gravity.valid ? ctx->sensor.gravity.x : 0.0f;
  float gy = ctx->sensor.gravity.valid ? ctx->sensor.gravity.y : 0.0f;

  if (gy > 0.7f) {
    vm->band_count = MATRIX_HEIGHT;
    vm->value_count = MATRIX_WIDTH;
    vm->orientation = 1;
  } else if (gy < -0.7f) {
    vm->band_count = MATRIX_HEIGHT;
    vm->value_count = MATRIX_WIDTH;
    vm->orientation = 3;
  } else if (gx > 0.7f) {
    vm->band_count = MATRIX_WIDTH;
    vm->value_count = MATRIX_HEIGHT;
    vm->orientation = 2;
  } else if (gx < -0.7f) {
    vm->band_count = MATRIX_WIDTH;
    vm->value_count = MATRIX_HEIGHT;
    vm->orientation = 0;
  }
}

static void map_xy(uint8_t orientation, uint8_t x, uint8_t y, uint8_t* out_x, uint8_t* out_y) {
  switch (orientation) {
    case 1:
      *out_x = MATRIX_WIDTH - y - 1;
      *out_y = x;
      break;
    case 2:
      *out_x = MATRIX_WIDTH - x - 1;
      *out_y = MATRIX_HEIGHT - y - 1;
      break;
    case 3:
      *out_x = y;
      *out_y = MATRIX_HEIGHT - x - 1;
      break;
    default:
      *out_x = x;
      *out_y = y;
      break;
  }
}

// Type coercion and arithmetic
static bottle_value_t add_values(bottle_value_t a, bottle_value_t b) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    return bottle_float(bottle_to_float(a) + bottle_to_float(b));
  }
  return bottle_int(bottle_to_int(a) + bottle_to_int(b));
}

static bottle_value_t sub_values(bottle_value_t a, bottle_value_t b) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    return bottle_float(bottle_to_float(a) - bottle_to_float(b));
  }
  return bottle_int(bottle_to_int(a) - bottle_to_int(b));
}

static bottle_value_t mul_values(bottle_value_t a, bottle_value_t b) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    return bottle_float(bottle_to_float(a) * bottle_to_float(b));
  }
  return bottle_int(bottle_to_int(a) * bottle_to_int(b));
}

static bottle_value_t div_values(bottle_value_t a, bottle_value_t b, bottle_error_t* err) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    float fb = bottle_to_float(b);
    if (fb == 0.0f) {
      bottle_error_set(err, 0, 0, "Division by zero");
      return bottle_float(0.0f);
    }
    return bottle_float(bottle_to_float(a) / fb);
  }
  int32_t ib = bottle_to_int(b);
  if (ib == 0) {
    bottle_error_set(err, 0, 0, "Division by zero");
    return bottle_int(0);
  }
  return bottle_int(bottle_to_int(a) / ib);
}

static bottle_value_t mod_values(bottle_value_t a, bottle_value_t b, bottle_error_t* err) {
  int32_t ia = bottle_to_int(a);
  int32_t ib = bottle_to_int(b);
  if (ib == 0) {
    bottle_error_set(err, 0, 0, "Modulo by zero");
    return bottle_int(0);
  }
  return bottle_int(ia % ib);
}

static bottle_value_t neg_value(bottle_value_t v) {
  if (v.type == BOTTLE_TYPE_FLOAT) {
    return bottle_float(-v.as.f);
  }
  return bottle_int(-bottle_to_int(v));
}

// Comparison operations
static bottle_value_t compare_lt(bottle_value_t a, bottle_value_t b) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    return bottle_bool(bottle_to_float(a) < bottle_to_float(b));
  }
  return bottle_bool(bottle_to_int(a) < bottle_to_int(b));
}

static bottle_value_t compare_le(bottle_value_t a, bottle_value_t b) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    return bottle_bool(bottle_to_float(a) <= bottle_to_float(b));
  }
  return bottle_bool(bottle_to_int(a) <= bottle_to_int(b));
}

static bottle_value_t compare_gt(bottle_value_t a, bottle_value_t b) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    return bottle_bool(bottle_to_float(a) > bottle_to_float(b));
  }
  return bottle_bool(bottle_to_int(a) > bottle_to_int(b));
}

static bottle_value_t compare_ge(bottle_value_t a, bottle_value_t b) {
  if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
    return bottle_bool(bottle_to_float(a) >= bottle_to_float(b));
  }
  return bottle_bool(bottle_to_int(a) >= bottle_to_int(b));
}

static bottle_value_t compare_eq(bottle_value_t a, bottle_value_t b) {
  if (a.type != b.type) {
    if (a.type == BOTTLE_TYPE_FLOAT || b.type == BOTTLE_TYPE_FLOAT) {
      return bottle_bool(bottle_to_float(a) == bottle_to_float(b));
    }
    return bottle_bool(bottle_to_int(a) == bottle_to_int(b));
  }
  
  switch (a.type) {
    case BOTTLE_TYPE_INT: return bottle_bool(a.as.i == b.as.i);
    case BOTTLE_TYPE_FLOAT: return bottle_bool(a.as.f == b.as.f);
    case BOTTLE_TYPE_BOOL: return bottle_bool(a.as.b == b.as.b);
    case BOTTLE_TYPE_COLOR:
      return bottle_bool(a.as.color.r == b.as.color.r &&
                        a.as.color.g == b.as.color.g &&
                        a.as.color.b == b.as.color.b);
    default: return bottle_bool(false);
  }
}

static bottle_value_t compare_ne(bottle_value_t a, bottle_value_t b) {
  bottle_value_t eq = compare_eq(a, b);
  return bottle_bool(!eq.as.b);
}

// VM initialization
void bottle_vm_init(bottle_vm_t* vm, const bottle_program_t* program) {
  memset(vm, 0, sizeof(*vm));
  bottle_error_init(&vm->error);
  
  // Initialize arrays
  for (uint8_t i = 0; i < program->array_count; i++) {
    const bottle_array_def_t* def = &program->arrays[i];
    for (uint8_t j = 0; j < def->length && j < MATRIX_WIDTH; j++) {
      vm->arrays[i][j] = def->initial_value;
    }
  }
  
  // Initialize scalars
  for (uint8_t i = 0; i < program->scalar_count; i++) {
    vm->scalars[i] = program->scalars[i].initial_value;
  }
  
  // Default orientation
  vm->orientation = 0;
  vm->band_count = MATRIX_WIDTH;
  vm->value_count = MATRIX_HEIGHT;
}

// Main execution loop
void bottle_vm_execute(bottle_vm_t* vm, const bottle_program_t* program,
                       uint16_t entry_offset, module_context_t* ctx) {
  if (!vm || !program || !ctx) return;
  if (vm->error.has_error) return;

  vm->pc = entry_offset;
  vm->instruction_count = 0;
  vm->stack_top = 0;

  while (vm->pc < program->bytecode_size) {
    // Timeout protection
    if (++vm->instruction_count > BOTTLE_MAX_INSTRUCTIONS_PER_FRAME) {
      bottle_error_set(&vm->error, program->debug_info[vm->pc], 0,
                      "Instruction limit exceeded (infinite loop?)");
      bottle_error_print(&vm->error, "Runtime");
      return;
    }

    uint8_t stack_before = vm->stack_top;
    uint16_t pc_before = vm->pc;

    uint8_t opcode = read_byte(program, &vm->pc);

    // Track opcodes that increase stack unexpectedly
    static uint16_t last_leak_pc = 0;

    switch ((bottle_opcode_t)opcode) {
      case OP_PUSH_CONST: {
        uint16_t index = read_u16(program, &vm->pc);
        if (index >= program->constant_count) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0, "Invalid constant index");
          return;
        }
        push(vm, program->constants[index]);
        break;
      }
      
      case OP_PUSH_SCALAR: {
        uint8_t index = read_byte(program, &vm->pc);
        if (index >= BOTTLE_MAX_SCALARS) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0, "Invalid scalar index");
          return;
        }
        push(vm, bottle_float(vm->scalars[index]));
        break;
      }
      
      case OP_PUSH_ARRAY: {
        uint8_t array_idx = read_byte(program, &vm->pc);
        bottle_value_t index_val = pop(vm);
        int32_t index = bottle_to_int(index_val);

        if (array_idx >= BOTTLE_MAX_ARRAYS) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0, "Invalid array index");
          return;
        }

        if (index < 0 || index >= program->arrays[array_idx].length) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0,
                          "Array index out of bounds: %d", index);
          return;
        }

        push(vm, bottle_int(vm->arrays[array_idx][index]));
        break;
      }
      
      case OP_POP_SCALAR: {
        uint8_t index = read_byte(program, &vm->pc);
        if (index >= BOTTLE_MAX_SCALARS) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0, "Invalid scalar index");
          return;
        }
        bottle_value_t value = pop(vm);

        vm->scalars[index] = bottle_to_float(value);
        break;
      }
      
      case OP_POP_ARRAY: {
        uint8_t array_idx = read_byte(program, &vm->pc);
        bottle_value_t value = pop(vm);
        bottle_value_t index_val = pop(vm);
        int32_t index = bottle_to_int(index_val);

        if (array_idx >= BOTTLE_MAX_ARRAYS) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0, "Invalid array index");
          return;
        }

        if (index < 0 || index >= program->arrays[array_idx].length) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0,
                          "Array index out of bounds: %d", index);
          return;
        }

        uint8_t byte_val = bottle_to_uint8(value);
        vm->arrays[array_idx][index] = byte_val;
        break;
      }
      
      case OP_DUP: {
        bottle_value_t value = peek(vm, 0);
        push(vm, value);
        break;
      }
      
      case OP_POP: {
        pop(vm);
        break;
      }
      
      // Arithmetic
      case OP_ADD: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, add_values(a, b));
        break;
      }
      
      case OP_SUB: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, sub_values(a, b));
        break;
      }
      
      case OP_MUL: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, mul_values(a, b));
        break;
      }
      
      case OP_DIV: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, div_values(a, b, &vm->error));
        if (vm->error.has_error) return;
        break;
      }
      
      case OP_MOD: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, mod_values(a, b, &vm->error));
        if (vm->error.has_error) return;
        break;
      }
      
      case OP_NEG: {
        bottle_value_t v = pop(vm);
        push(vm, neg_value(v));
        break;
      }
      
      // Comparison
      case OP_LT: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);

        push(vm, compare_lt(a, b));
        break;
      }
      
      case OP_LE: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, compare_le(a, b));
        break;
      }
      
      case OP_GT: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, compare_gt(a, b));
        break;
      }
      
      case OP_GE: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, compare_ge(a, b));
        break;
      }
      
      case OP_EQ: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, compare_eq(a, b));
        break;
      }
      
      case OP_NE: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, compare_ne(a, b));
        break;
      }
      
      // Logical
      case OP_AND: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, bottle_bool(bottle_is_truthy(a) && bottle_is_truthy(b)));
        break;
      }
      
      case OP_OR: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, bottle_bool(bottle_is_truthy(a) || bottle_is_truthy(b)));
        break;
      }
      
      case OP_NOT: {
        bottle_value_t v = pop(vm);
        push(vm, bottle_bool(!bottle_is_truthy(v)));
        break;
      }
      
      // Control flow
      case OP_JUMP: {
        uint16_t offset_raw = read_u16(program, &vm->pc);
        int16_t offset = (int16_t)offset_raw;  // Interpret as signed for backward jumps

        vm->pc = vm->pc + offset;  // Relative jump (can be negative)
        break;
      }
      
      case OP_JUMP_IF_FALSE: {
        uint16_t offset_raw = read_u16(program, &vm->pc);
        int16_t offset = (int16_t)offset_raw;  // Interpret as signed
        bottle_value_t condition = peek(vm, 0);
        if (!bottle_is_truthy(condition)) {
          vm->pc = vm->pc + offset;  // Relative jump (can be negative)
        }
        break;
      }

      case OP_JUMP_IF_TRUE: {
        uint16_t offset_raw = read_u16(program, &vm->pc);
        int16_t offset = (int16_t)offset_raw;  // Interpret as signed
        bottle_value_t condition = peek(vm, 0);
        if (bottle_is_truthy(condition)) {
          vm->pc = vm->pc + offset;  // Relative jump (can be negative)
        }
        break;
      }
            // Built-in math functions
      case OP_CALL_MAX: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, builtin_max(a, b));
        break;
      }
      
      case OP_CALL_MIN: {
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, builtin_min(a, b));
        break;
      }
      
      case OP_CALL_CLAMP: {
        bottle_value_t hi = pop(vm);
        bottle_value_t lo = pop(vm);
        bottle_value_t v = pop(vm);
        push(vm, builtin_clamp(v, lo, hi));
        break;
      }
      
      case OP_CALL_ABS: {
        bottle_value_t x = pop(vm);
        push(vm, builtin_abs(x));
        break;
      }
      
      case OP_CALL_SQRT: {
        bottle_value_t x = pop(vm);
        push(vm, builtin_sqrt(x));
        break;
      }
      
      case OP_CALL_SIN: {
        bottle_value_t x = pop(vm);
        push(vm, builtin_sin(x));
        break;
      }
      
      case OP_CALL_COS: {
        bottle_value_t x = pop(vm);
        push(vm, builtin_cos(x));
        break;
      }
      
      case OP_CALL_RANDOM: {
        bottle_value_t max_val = pop(vm);
        bottle_value_t min_val = pop(vm);
        push(vm, builtin_random(min_val, max_val));
        break;
      }
      
      case OP_CALL_MILLIS: {
        push(vm, builtin_millis(ctx->now_ms));
        break;
      }

      case OP_CALL_MAX_HEIGHT: {
        // Return value_count which represents the logical height after rotation
        // This is the actual physical height in the current orientation
        push(vm, bottle_int(vm->value_count));
        break;
      }

      // Color functions
      case OP_CALL_HSV: {
        bottle_value_t v = pop(vm);
        bottle_value_t s = pop(vm);
        bottle_value_t h = pop(vm);
        push(vm, builtin_hsv(h, s, v));
        break;
      }
      
      case OP_CALL_RGB: {
        bottle_value_t b = pop(vm);
        bottle_value_t g = pop(vm);
        bottle_value_t r = pop(vm);
        push(vm, builtin_rgb(r, g, b));
        break;
      }
      
      case OP_CALL_BLEND: {
        bottle_value_t amount = pop(vm);
        bottle_value_t b = pop(vm);
        bottle_value_t a = pop(vm);
        push(vm, builtin_blend(a, b, amount));
        break;
      }
      
      // Hardware APIs
      case OP_READ_SPECTRUM: {
        uint8_t array_idx = read_byte(program, &vm->pc);
        if (array_idx >= BOTTLE_MAX_ARRAYS) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0, "Invalid array index");
          return;
        }

        // Read spectrum data into array
        uint8_t count = min(program->arrays[array_idx].length, (uint8_t)MATRIX_WIDTH);
        for (uint8_t i = 0; i < count; i++) {
          if (vm->orientation & 1) {
            // Rotated: average two adjacent bands
            vm->arrays[array_idx][i] = (ctx->sensor.spectrum[i * 2] +
                                        ctx->sensor.spectrum[i * 2 + 1]) / 2;
          } else {
            vm->arrays[array_idx][i] = ctx->sensor.spectrum[i];
          }
        }
        break;
      }
      
      case OP_READ_ACCEL: {
        update_orientation(vm, ctx);
        break;
      }
      
      case OP_CLEAR_LEDS: {
        ctx->led.clear();
        break;
      }
      
      case OP_SET_LED: {
        bottle_value_t color_val = pop(vm);
        bottle_value_t y_val = pop(vm);
        bottle_value_t x_val = pop(vm);

        if (color_val.type != BOTTLE_TYPE_COLOR) {
          bottle_error_set(&vm->error, program->debug_info[vm->pc], 0,
                          "SET_LED requires color value");
          return;
        }

        int32_t x = bottle_to_int(x_val);
        int32_t y = bottle_to_int(y_val);

        // Apply orientation transform
        uint8_t px, py;
        map_xy(vm->orientation, (uint8_t)x, (uint8_t)y, &px, &py);

        // Bounds check
        if (px >= MATRIX_WIDTH || py >= MATRIX_HEIGHT) {
          // Silently ignore out-of-bounds (common in rotated mode)
          break;
        }

        ctx->led.set(px, py, color_val.as.color);
        break;
      }

      case OP_SHOW_LEDS: {
        ctx->led.show();
        break;
      }
      
      // Debug
      case OP_PRINT: {
        bottle_value_t value = pop(vm);
        builtin_print(value);
        break;
      }
      
      // Special
      case OP_HALT: {
        return;
      }
      
      default: {
        bottle_error_set(&vm->error, program->debug_info[vm->pc], 0,
                        "Unknown opcode: 0x%02X", opcode);
        bottle_error_print(&vm->error, "Runtime");
        return;
      }
    }

    // Check for stack leaks after each instruction
    int8_t stack_delta = (int8_t)vm->stack_top - (int8_t)stack_before;

    if (vm->error.has_error) {
      bottle_error_print(&vm->error, "Runtime");
      return;
    }
  }
}

// Run loop section with frame timing
void bottle_vm_run_loop(bottle_vm_t* vm, const bottle_program_t* program, module_context_t* ctx) {
  if (!vm || !program || !ctx) return;
  if (vm->error.has_error) return;
  if (!program->has_loop) return;
  
  // Frame rate limiting
  if (program->frame_ms > 0) {
    uint32_t elapsed = ctx->now_ms - vm->last_frame_ms;
    if (elapsed < program->frame_ms) {
      return; // Skip this frame
    }
    vm->last_frame_ms = ctx->now_ms;
  }
  
  // Execute loop section
  bottle_vm_execute(vm, program, program->loop_offset, ctx);
}

// Debug status
const char* bottle_vm_status_json(const bottle_vm_t* vm) {
  static char buffer[256];
  snprintf(buffer, sizeof(buffer),
           "{\"orientation\":%d,\"band_count\":%d,\"value_count\":%d,"
           "\"stack_top\":%d,\"instruction_count\":%d,\"has_error\":%s}",
           vm->orientation, vm->band_count, vm->value_count,
           vm->stack_top, vm->instruction_count,
           vm->error.has_error ? "true" : "false");
  return buffer;
}
