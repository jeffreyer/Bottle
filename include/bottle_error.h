#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOTTLE_ERROR_MSG_LEN 128

// Error context for compilation and runtime
typedef struct {
  bool has_error;
  uint16_t line;
  uint16_t column;
  char message[BOTTLE_ERROR_MSG_LEN];
} bottle_error_t;

// Initialize error context
static inline void bottle_error_init(bottle_error_t* err) {
  err->has_error = false;
  err->line = 0;
  err->column = 0;
  err->message[0] = '\0';
}

// Set error with formatted message
void bottle_error_set(bottle_error_t* err, uint16_t line, uint16_t column, const char* format, ...);

// Print error to Serial
void bottle_error_print(const bottle_error_t* err, const char* phase);

#ifdef __cplusplus
}
#endif
