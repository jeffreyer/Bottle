#include "bottle_error.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

void bottle_error_set(bottle_error_t* err, uint16_t line, uint16_t column, const char* format, ...) {
  if (!err) return;

  err->has_error = true;
  err->line = line;
  err->column = column;

  va_list args;
  va_start(args, format);
  vsnprintf(err->message, BOTTLE_ERROR_MSG_LEN, format, args);
  va_end(args);
}

void bottle_error_print(const bottle_error_t* err, const char* phase) {
  if (!err || !err->has_error) return;

  if (err->column > 0) {
    Serial.printf("[Bottle %s Error] Line %d, Col %d: %s\n",
                  phase, err->line, err->column, err->message);
  } else {
    Serial.printf("[Bottle %s Error] Line %d: %s\n",
                  phase, err->line, err->message);
  }
}
