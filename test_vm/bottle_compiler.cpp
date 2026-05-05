#include "bottle_compiler.h"
#include "common.h"
#include <string.h>
#include <ctype.h>
#include <Arduino.h>
#include <math.h>

// Token types
typedef enum {
  TOKEN_EOF = 0,
  TOKEN_ERROR,
  
  // Literals
  TOKEN_NUMBER,
  TOKEN_IDENTIFIER,
  TOKEN_STRING,
  
  // Keywords
  TOKEN_RUNTIME,
  TOKEN_MODULE,
  TOKEN_STATE,
  TOKEN_CONFIG,
  TOKEN_FRAME_MS,
  TOKEN_SETUP,
  TOKEN_LOOP,
  TOKEN_UNLOAD,
  TOKEN_IF,
  TOKEN_ELSE,
  TOKEN_FOR,
  TOKEN_IN,
  TOKEN_EVERY,
  TOKEN_USE,
  TOKEN_REQUIRE,
  TOKEN_TYPE,
  TOKEN_LABEL,
  TOKEN_DEFAULT,
  TOKEN_OPTIONS,
  
  // Operators
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_STAR,
  TOKEN_SLASH,
  TOKEN_PERCENT,
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  TOKEN_BANG_EQUAL,
  TOKEN_LESS,
  TOKEN_LESS_EQUAL,
  TOKEN_GREATER,
  TOKEN_GREATER_EQUAL,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_NOT,
  
  // Delimiters
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_COMMA,
  TOKEN_DOT,
} token_type_t;

typedef struct {
  token_type_t type;
  const char* start;
  uint16_t length;
  uint16_t line;
  uint16_t column;
  
  // For TOKEN_NUMBER
  bool is_float;
  union {
    int32_t int_value;
    float float_value;
  };
} token_t;

// Tokenizer state
typedef struct {
  const char* start;
  const char* current;
  uint16_t line;
  uint16_t line_start;
} tokenizer_t;

// Compiler state
typedef struct {
  tokenizer_t tokenizer;
  token_t current;
  token_t previous;
  bottle_program_t* program;
  
  // Symbol tables
  struct {
    char name[BOTTLE_NAME_LEN];
    uint8_t index;
  } arrays[BOTTLE_MAX_ARRAYS];
  uint8_t array_count;
  
  struct {
    char name[BOTTLE_NAME_LEN];
    uint8_t index;
  } scalars[BOTTLE_MAX_SCALARS];
  uint8_t scalar_count;
  
  // Temporal tracking (for "every" clauses)
  uint8_t temporal_count;
  
  // Loop context
  struct {
    char iterator_name[BOTTLE_NAME_LEN];
    uint8_t array_index;
    uint8_t iterator_scalar_index;
    bool active;
  } loop_context;
  
  // Jump patching
  struct {
    uint16_t offset;
    uint16_t target;
  } jumps[32];
  uint8_t jump_count;
  
} compiler_t;

// Forward declarations
static void advance(compiler_t* c);
static bool match(compiler_t* c, token_type_t type);
static bool check(compiler_t* c, token_type_t type);
static void consume(compiler_t* c, token_type_t type, const char* message);
static void expression(compiler_t* c);
static void statement(compiler_t* c);
static void block(compiler_t* c);

// Error reporting
static void error_at(compiler_t* c, token_t* token, const char* message) {
  if (c->program->error.has_error) return;
  bottle_error_set(&c->program->error, token->line, token->column, "%s", message);
}

static void error(compiler_t* c, const char* message) {
  error_at(c, &c->previous, message);
}

static void error_at_current(compiler_t* c, const char* message) {
  error_at(c, &c->current, message);
}

// Bytecode emission
static void emit_byte(compiler_t* c, uint8_t byte) {
  if (c->program->bytecode_size >= BOTTLE_MAX_BYTECODE) {
    error(c, "Bytecode buffer overflow");
    return;
  }
  c->program->bytecode[c->program->bytecode_size] = byte;
  c->program->debug_info[c->program->bytecode_size] = c->previous.line;
  c->program->bytecode_size++;
}

static void emit_bytes(compiler_t* c, uint8_t byte1, uint8_t byte2) {
  emit_byte(c, byte1);
  emit_byte(c, byte2);
}

static void emit_u16(compiler_t* c, uint16_t value) {
  emit_byte(c, (value >> 8) & 0xFF);
  emit_byte(c, value & 0xFF);
}

static void emit_opcode(compiler_t* c, bottle_opcode_t op) {
  emit_byte(c, (uint8_t)op);
}

static uint16_t emit_jump(compiler_t* c, bottle_opcode_t op) {
  emit_opcode(c, op);
  emit_u16(c, 0xFFFF); // Placeholder
  return c->program->bytecode_size - 2;
}

static void patch_jump(compiler_t* c, uint16_t offset) {
  uint16_t jump = c->program->bytecode_size - offset - 2;
  c->program->bytecode[offset] = (jump >> 8) & 0xFF;
  c->program->bytecode[offset + 1] = jump & 0xFF;
}

static uint16_t current_offset(compiler_t* c) {
  return c->program->bytecode_size;
}

// Constant pool
static uint16_t add_constant(compiler_t* c, bottle_value_t value) {
  if (c->program->constant_count >= BOTTLE_MAX_CONSTANTS) {
    error(c, "Too many constants");
    return 0;
  }
  c->program->constants[c->program->constant_count] = value;
  return c->program->constant_count++;
}

static void emit_constant(compiler_t* c, bottle_value_t value) {
  uint16_t index = add_constant(c, value);
  emit_opcode(c, OP_PUSH_CONST);
  emit_u16(c, index);
}

// Tokenizer implementation
static void init_tokenizer(tokenizer_t* t, const char* source) {
  t->start = source;
  t->current = source;
  t->line = 1;
  t->line_start = 0;
}

static bool is_at_end(tokenizer_t* t) {
  return *t->current == '\0';
}

static char advance_char(tokenizer_t* t) {
  t->current++;
  return t->current[-1];
}

static char peek(tokenizer_t* t) {
  return *t->current;
}

static char peek_next(tokenizer_t* t) {
  if (is_at_end(t)) return '\0';
  return t->current[1];
}

static bool match_char(tokenizer_t* t, char expected) {
  if (is_at_end(t)) return false;
  if (*t->current != expected) return false;
  t->current++;
  return true;
}

static void skip_whitespace(tokenizer_t* t) {
  for (;;) {
    char c = peek(t);
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance_char(t);
        break;
      case '\n':
        t->line++;
        t->line_start = (uint16_t)(t->current - t->start) + 1;
        advance_char(t);
        break;
      case '/':
        if (peek_next(t) == '/') {
          while (peek(t) != '\n' && !is_at_end(t)) advance_char(t);
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static token_t make_token(tokenizer_t* t, token_type_t type) {
  token_t token;
  token.type = type;
  token.start = t->start;
  token.length = (uint16_t)(t->current - t->start);
  token.line = t->line;
  token.column = (uint16_t)(t->start - (t->start - t->line_start));
  token.is_float = false;
  return token;
}

static token_t error_token(tokenizer_t* t, const char* message) {
  token_t token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (uint16_t)strlen(message);
  token.line = t->line;
  token.column = (uint16_t)(t->current - (t->start - t->line_start));
  return token;
}

static token_type_t check_keyword(tokenizer_t* t, int start, int length, const char* rest, token_type_t type) {
  if (t->current - t->start == start + length && memcmp(t->start + start, rest, length) == 0) {
    return type;
  }
  return TOKEN_IDENTIFIER;
}

static token_type_t identifier_type(tokenizer_t* t) {
  switch (t->start[0]) {
    case 'a': return check_keyword(t, 1, 2, "nd", TOKEN_AND);
    case 'c': return check_keyword(t, 1, 5, "onfig", TOKEN_CONFIG);
    case 'd': return check_keyword(t, 1, 6, "efault", TOKEN_DEFAULT);
    case 'e':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'l': return check_keyword(t, 2, 2, "se", TOKEN_ELSE);
          case 'v': return check_keyword(t, 2, 3, "ery", TOKEN_EVERY);
        }
      }
      break;
    case 'f':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'o': return check_keyword(t, 2, 1, "r", TOKEN_FOR);
          case 'r': return check_keyword(t, 2, 6, "ame_ms", TOKEN_FRAME_MS);
        }
      }
      break;
    case 'i':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'f': return check_keyword(t, 2, 0, "", TOKEN_IF);
          case 'n': return check_keyword(t, 2, 0, "", TOKEN_IN);
        }
      }
      break;
    case 'l':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'a': return check_keyword(t, 2, 3, "bel", TOKEN_LABEL);
          case 'o': return check_keyword(t, 2, 2, "op", TOKEN_LOOP);
        }
      }
      break;
    case 'm': return check_keyword(t, 1, 5, "odule", TOKEN_MODULE);
    case 'n': return check_keyword(t, 1, 2, "ot", TOKEN_NOT);
    case 'o':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'p': return check_keyword(t, 2, 5, "tions", TOKEN_OPTIONS);
          case 'r': return check_keyword(t, 2, 0, "", TOKEN_OR);
        }
      }
      break;
    case 'r':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'e': return check_keyword(t, 2, 5, "quire", TOKEN_REQUIRE);
          case 'u': return check_keyword(t, 2, 5, "ntime", TOKEN_RUNTIME);
        }
      }
      break;
    case 's':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'e': return check_keyword(t, 2, 3, "tup", TOKEN_SETUP);
          case 't': return check_keyword(t, 2, 3, "ate", TOKEN_STATE);
        }
      }
      break;
    case 't': return check_keyword(t, 1, 3, "ype", TOKEN_TYPE);
    case 'u':
      if (t->current - t->start > 1) {
        switch (t->start[1]) {
          case 'n': return check_keyword(t, 2, 4, "load", TOKEN_UNLOAD);
          case 's': return check_keyword(t, 2, 1, "e", TOKEN_USE);
        }
      }
      break;
  }
  return TOKEN_IDENTIFIER;
}

static token_t identifier(tokenizer_t* t) {
  while (isalnum((unsigned char)peek(t)) || peek(t) == '_' || peek(t) == '-' || peek(t) == '.' || peek(t) == '@') advance_char(t);
  return make_token(t, identifier_type(t));
}

static token_t number(tokenizer_t* t) {
  while (isdigit((unsigned char)peek(t))) advance_char(t);

  bool is_float = false;
  if (peek(t) == '.' && isdigit((unsigned char)peek_next(t))) {
    is_float = true;
    advance_char(t);
    while (isdigit(peek(t))) advance_char(t);
  }
  
  token_t token = make_token(t, TOKEN_NUMBER);
  token.is_float = is_float;
  
  char buffer[32];
  int len = min((int)token.length, 31);
  memcpy(buffer, token.start, len);
  buffer[len] = '\0';
  
  if (is_float) {
    token.float_value = atof(buffer);
  } else {
    token.int_value = atoi(buffer);
  }
  
  return token;
}

static token_t string(tokenizer_t* t) {
  while (peek(t) != '"' && !is_at_end(t)) {
    if (peek(t) == '\n') t->line++;
    advance_char(t);
  }
  
  if (is_at_end(t)) return error_token(t, "Unterminated string");
  
  advance_char(t); // Closing quote
  return make_token(t, TOKEN_STRING);
}

static token_t scan_token(tokenizer_t* t) {
  skip_whitespace(t);
  t->start = t->current;

  if (is_at_end(t)) {
    return make_token(t, TOKEN_EOF);
  }

  char c = advance_char(t);
  unsigned char uc = (unsigned char)c;

  if (isalpha(uc) || c == '_') {
    return identifier(t);
  }
  if (isdigit(uc)) return number(t);

  // Debug: print character being scanned
  if (c == '{' || c == '}') {
    Serial.printf("[Tokenizer] Scanning '%c' (0x%02x)\n", c, (unsigned char)c);
  }

  switch (c) {
    case '(': return make_token(t, TOKEN_LEFT_PAREN);
    case ')': return make_token(t, TOKEN_RIGHT_PAREN);
    case '[': return make_token(t, TOKEN_LEFT_BRACKET);
    case ']': return make_token(t, TOKEN_RIGHT_BRACKET);
    case '{':
      Serial.printf("[Tokenizer] Matched '{', returning TOKEN_LEFT_BRACE (%d)\n", TOKEN_LEFT_BRACE);
      return make_token(t, TOKEN_LEFT_BRACE);
    case '}': return make_token(t, TOKEN_RIGHT_BRACE);
    case ',': return make_token(t, TOKEN_COMMA);
    case '.': return make_token(t, TOKEN_DOT);
    case '+': return make_token(t, TOKEN_PLUS);
    case '-': return make_token(t, TOKEN_MINUS);
    case '*': return make_token(t, TOKEN_STAR);
    case '/': return make_token(t, TOKEN_SLASH);
    case '%': return make_token(t, TOKEN_PERCENT);
    case '!':
      return make_token(t, match_char(t, '=') ? TOKEN_BANG_EQUAL : TOKEN_NOT);
    case '=':
      return make_token(t, match_char(t, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return make_token(t, match_char(t, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return make_token(t, match_char(t, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"': return string(t);
  }
  
  return error_token(t, "Unexpected character");
}

// Parser helpers
static void advance(compiler_t* c) {
  c->previous = c->current;

  for (;;) {
    c->current = scan_token(&c->tokenizer);
    if (c->current.type != TOKEN_ERROR) break;

    error_at_current(c, c->current.start);
  }
}

static bool check(compiler_t* c, token_type_t type) {
  return c->current.type == type;
}

static bool match(compiler_t* c, token_type_t type) {
  if (!check(c, type)) return false;
  advance(c);
  return true;
}

static void consume(compiler_t* c, token_type_t type, const char* message) {
  if (c->current.type == type) {
    advance(c);
    return;
  }
  error_at_current(c, message);
}

static bool identifiers_equal(token_t* a, token_t* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static bool identifier_equals_string(token_t* token, const char* str) {
  size_t len = strlen(str);
  if (token->length != len) return false;
  return memcmp(token->start, str, len) == 0;
}

static void copy_identifier(char* dest, token_t* token, size_t max_len) {
  size_t len = min((size_t)token->length, max_len - 1);
  memcpy(dest, token->start, len);
  dest[len] = '\0';
}

// Symbol table
static int8_t find_array(compiler_t* c, token_t* name) {
  for (uint8_t i = 0; i < c->array_count; i++) {
    if (identifier_equals_string(name, c->arrays[i].name)) {
      return i;
    }
  }
  return -1;
}

static int8_t find_scalar(compiler_t* c, token_t* name) {
  for (uint8_t i = 0; i < c->scalar_count; i++) {
    if (identifier_equals_string(name, c->scalars[i].name)) {
      return i;
    }
  }
  return -1;
}

static uint8_t add_array(compiler_t* c, token_t* name) {
  if (c->array_count >= BOTTLE_MAX_ARRAYS) {
    error(c, "Too many arrays");
    return 0;
  }
  copy_identifier(c->arrays[c->array_count].name, name, BOTTLE_NAME_LEN);
  c->arrays[c->array_count].index = c->array_count;
  return c->array_count++;
}

static uint8_t add_scalar(compiler_t* c, token_t* name) {
  if (c->scalar_count >= BOTTLE_MAX_SCALARS) {
    error(c, "Too many scalars");
    return 0;
  }
  copy_identifier(c->scalars[c->scalar_count].name, name, BOTTLE_NAME_LEN);
  c->scalars[c->scalar_count].index = c->scalar_count;
  return c->scalar_count++;
}

// Expression parsing (precedence climbing)
static void parse_precedence(compiler_t* c, int precedence);

static void grouping(compiler_t* c) {
  expression(c);
  consume(c, TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}

static void number(compiler_t* c) {
  if (c->previous.is_float) {
    emit_constant(c, bottle_float(c->previous.float_value));
  } else {
    emit_constant(c, bottle_int(c->previous.int_value));
  }
}

static void unary(compiler_t* c) {
  token_type_t op = c->previous.type;
  
  parse_precedence(c, 7); // Unary precedence
  
  switch (op) {
    case TOKEN_MINUS: emit_opcode(c, OP_NEG); break;
    case TOKEN_NOT: emit_opcode(c, OP_NOT); break;
    default: return;
  }
}

static void binary(compiler_t* c) {
  token_type_t op = c->previous.type;
  
  // Get precedence for this operator
  int prec = 0;
  switch (op) {
    case TOKEN_OR: prec = 1; break;
    case TOKEN_AND: prec = 2; break;
    case TOKEN_EQUAL_EQUAL:
    case TOKEN_BANG_EQUAL:
    case TOKEN_LESS:
    case TOKEN_LESS_EQUAL:
    case TOKEN_GREATER:
    case TOKEN_GREATER_EQUAL:
      prec = 3; break;
    case TOKEN_PLUS:
    case TOKEN_MINUS:
      prec = 5; break;
    case TOKEN_STAR:
    case TOKEN_SLASH:
    case TOKEN_PERCENT:
      prec = 6; break;
    default: break;
  }
  
  parse_precedence(c, prec + 1);
  
  switch (op) {
    case TOKEN_PLUS: emit_opcode(c, OP_ADD); break;
    case TOKEN_MINUS: emit_opcode(c, OP_SUB); break;
    case TOKEN_STAR: emit_opcode(c, OP_MUL); break;
    case TOKEN_SLASH: emit_opcode(c, OP_DIV); break;
    case TOKEN_PERCENT: emit_opcode(c, OP_MOD); break;
    case TOKEN_EQUAL_EQUAL: emit_opcode(c, OP_EQ); break;
    case TOKEN_BANG_EQUAL: emit_opcode(c, OP_NE); break;
    case TOKEN_LESS: emit_opcode(c, OP_LT); break;
    case TOKEN_LESS_EQUAL: emit_opcode(c, OP_LE); break;
    case TOKEN_GREATER: emit_opcode(c, OP_GT); break;
    case TOKEN_GREATER_EQUAL: emit_opcode(c, OP_GE); break;
    case TOKEN_AND: emit_opcode(c, OP_AND); break;
    case TOKEN_OR: emit_opcode(c, OP_OR); break;
    default: return;
  }
}

static void call(compiler_t* c) {
  token_t func_name = c->previous;
  
  consume(c, TOKEN_LEFT_PAREN, "Expect '(' after function name");
  
  uint8_t arg_count = 0;
  if (!check(c, TOKEN_RIGHT_PAREN)) {
    do {
      expression(c);
      arg_count++;
      if (arg_count > 8) {
        error(c, "Too many arguments");
        return;
      }
    } while (match(c, TOKEN_COMMA));
  }
  
  consume(c, TOKEN_RIGHT_PAREN, "Expect ')' after arguments");
  
  // Emit appropriate opcode based on function name
  if (identifier_equals_string(&func_name, "max")) {
    if (arg_count != 2) error(c, "max() requires 2 arguments");
    emit_opcode(c, OP_CALL_MAX);
  } else if (identifier_equals_string(&func_name, "min")) {
    if (arg_count != 2) error(c, "min() requires 2 arguments");
    emit_opcode(c, OP_CALL_MIN);
  } else if (identifier_equals_string(&func_name, "clamp")) {
    if (arg_count != 3) error(c, "clamp() requires 3 arguments");
    emit_opcode(c, OP_CALL_CLAMP);
  } else if (identifier_equals_string(&func_name, "abs")) {
    if (arg_count != 1) error(c, "abs() requires 1 argument");
    emit_opcode(c, OP_CALL_ABS);
  } else if (identifier_equals_string(&func_name, "sqrt")) {
    if (arg_count != 1) error(c, "sqrt() requires 1 argument");
    emit_opcode(c, OP_CALL_SQRT);
  } else if (identifier_equals_string(&func_name, "sin")) {
    if (arg_count != 1) error(c, "sin() requires 1 argument");
    emit_opcode(c, OP_CALL_SIN);
  } else if (identifier_equals_string(&func_name, "cos")) {
    if (arg_count != 1) error(c, "cos() requires 1 argument");
    emit_opcode(c, OP_CALL_COS);
  } else if (identifier_equals_string(&func_name, "random")) {
    if (arg_count != 2) error(c, "random() requires 2 arguments");
    emit_opcode(c, OP_CALL_RANDOM);
  } else if (identifier_equals_string(&func_name, "millis")) {
    if (arg_count != 0) error(c, "millis() requires 0 arguments");
    emit_opcode(c, OP_CALL_MILLIS);
  } else if (identifier_equals_string(&func_name, "hsv")) {
    if (arg_count != 3) error(c, "hsv() requires 3 arguments");
    emit_opcode(c, OP_CALL_HSV);
  } else if (identifier_equals_string(&func_name, "rgb")) {
    if (arg_count != 3) error(c, "rgb() requires 3 arguments");
    emit_opcode(c, OP_CALL_RGB);
  } else if (identifier_equals_string(&func_name, "blend")) {
    if (arg_count != 3) error(c, "blend() requires 3 arguments");
    emit_opcode(c, OP_CALL_BLEND);
  } else {
    error(c, "Unknown function");
  }
}

static void variable(compiler_t* c) {
  token_t name = c->previous;
  
  // Check for array access
  if (match(c, TOKEN_LEFT_BRACKET)) {
    int8_t array_idx = find_array(c, &name);
    if (array_idx < 0) {
      error(c, "Undefined array");
      return;
    }
    
    expression(c); // Index expression
    consume(c, TOKEN_RIGHT_BRACKET, "Expect ']' after array index");
    
    emit_opcode(c, OP_PUSH_ARRAY);
    emit_byte(c, (uint8_t)array_idx);
    return;
  }
  
  // Check for special constants
  if (identifier_equals_string(&name, "WIDTH")) {
    emit_constant(c, bottle_int(MATRIX_WIDTH));
    return;
  }
  if (identifier_equals_string(&name, "HEIGHT")) {
    emit_constant(c, bottle_int(MATRIX_HEIGHT));
    return;
  }
  
  // Check for loop iterator
  if (c->loop_context.active && identifier_equals_string(&name, c->loop_context.iterator_name)) {
    Serial.printf("[COMPILER] variable(): Found loop iterator '%.*s', using scalar[%d]\n",
                  name.length, name.start, c->loop_context.iterator_scalar_index);
    emit_opcode(c, OP_PUSH_SCALAR);
    emit_byte(c, c->loop_context.iterator_scalar_index);
    return;
  }

  // Check for scalar variable
  int8_t scalar_idx = find_scalar(c, &name);
  if (scalar_idx >= 0) {
    Serial.printf("[COMPILER] variable(): Found scalar '%.*s' at index %d\n",
                  name.length, name.start, scalar_idx);
    emit_opcode(c, OP_PUSH_SCALAR);
    emit_byte(c, (uint8_t)scalar_idx);
    return;
  }
  
  // Check for config variable
  for (uint8_t i = 0; i < c->program->config_count; i++) {
    if (identifier_equals_string(&name, c->program->configs[i].key)) {
      // Config values are loaded as scalars at runtime
      emit_opcode(c, OP_PUSH_SCALAR);
      emit_byte(c, (uint8_t)(c->scalar_count + i));
      return;
    }
  }
  
  error(c, "Undefined variable");
}

static void primary(compiler_t* c) {
  switch (c->current.type) {
    case TOKEN_NUMBER:
      advance(c);
      number(c);
      break;
    case TOKEN_IDENTIFIER:
      advance(c);
      if (check(c, TOKEN_LEFT_PAREN)) {
        call(c);
      } else {
        variable(c);
      }
      break;
    case TOKEN_LEFT_PAREN:
      advance(c);
      grouping(c);
      break;
    case TOKEN_MINUS:
    case TOKEN_NOT:
      advance(c);
      unary(c);
      break;
    default:
      error_at_current(c, "Expect expression");
      break;
  }
}

static void parse_precedence(compiler_t* c, int precedence) {
  primary(c);
  
  while (true) {
    int next_prec = 0;
    switch (c->current.type) {
      case TOKEN_OR: next_prec = 1; break;
      case TOKEN_AND: next_prec = 2; break;
      case TOKEN_EQUAL_EQUAL:
      case TOKEN_BANG_EQUAL:
      case TOKEN_LESS:
      case TOKEN_LESS_EQUAL:
      case TOKEN_GREATER:
      case TOKEN_GREATER_EQUAL:
        next_prec = 3; break;
      case TOKEN_PLUS:
      case TOKEN_MINUS:
        next_prec = 5; break;
      case TOKEN_STAR:
      case TOKEN_SLASH:
      case TOKEN_PERCENT:
        next_prec = 6; break;
      default:
        return;
    }
    
    if (next_prec < precedence) return;
    
    advance(c);
    binary(c);
  }
}

static void expression(compiler_t* c) {
  parse_precedence(c, 1);
}

// Statement parsing
static void expression_statement(compiler_t* c) {
  expression(c);
  emit_opcode(c, OP_POP); // Discard result
}

static void assignment_statement(compiler_t* c) {
  token_t target = c->previous;
  
  // Check if it's an array assignment
  if (match(c, TOKEN_LEFT_BRACKET)) {
    int8_t array_idx = find_array(c, &target);
    if (array_idx < 0) {
      error(c, "Undefined array");
      return;
    }
    
    expression(c); // Index
    consume(c, TOKEN_RIGHT_BRACKET, "Expect ']'");
    consume(c, TOKEN_EQUAL, "Expect '='");
    
    expression(c); // Value
    
    // Check for "every" temporal clause
    if (match(c, TOKEN_EVERY)) {
      if (!check(c, TOKEN_NUMBER)) {
        error(c, "Expect number after 'every'");
        return;
      }
      advance(c);
      int32_t interval_ms = c->previous.int_value;
      
      if (!match(c, TOKEN_IDENTIFIER) || !identifier_equals_string(&c->previous, "ms")) {
        error(c, "Expect 'ms' after interval");
        return;
      }
      
      // Allocate temporal scalar for this clause
      char temporal_name[BOTTLE_NAME_LEN];
      snprintf(temporal_name, BOTTLE_NAME_LEN, "_tick_%d", c->temporal_count);
      token_t temporal_token;
      temporal_token.start = temporal_name;
      temporal_token.length = strlen(temporal_name);
      uint8_t temporal_idx = add_scalar(c, &temporal_token);
      
      // Add to program scalars
      bottle_scalar_def_t* scalar = &c->program->scalars[c->program->scalar_count++];
      strncpy(scalar->name, temporal_name, BOTTLE_NAME_LEN);
      scalar->initial_value = 0.0f;
      
      c->temporal_count++;
      
      // Generate: if (millis() - last_tick >= interval) { last_tick = millis(); assignment; }
      emit_opcode(c, OP_CALL_MILLIS);
      emit_opcode(c, OP_PUSH_SCALAR);
      emit_byte(c, temporal_idx);
      emit_opcode(c, OP_SUB);
      emit_constant(c, bottle_int(interval_ms));
      emit_opcode(c, OP_GE);

      uint16_t skip_jump = emit_jump(c, OP_JUMP_IF_FALSE);

      // Pop the condition result (true branch)
      emit_opcode(c, OP_POP);

      // Update last_tick
      emit_opcode(c, OP_CALL_MILLIS);
      emit_opcode(c, OP_POP_SCALAR);
      emit_byte(c, temporal_idx);

      // Do the assignment (value and index are already on stack)
      emit_opcode(c, OP_POP_ARRAY);
      emit_byte(c, (uint8_t)array_idx);

      uint16_t end_jump = emit_jump(c, OP_JUMP);

      patch_jump(c, skip_jump);
      // Pop the condition result (false branch)
      emit_opcode(c, OP_POP);
      // Also pop the value and index that weren't consumed
      emit_opcode(c, OP_POP); // Pop value
      emit_opcode(c, OP_POP); // Pop index

      patch_jump(c, end_jump);
    } else {
      emit_opcode(c, OP_POP_ARRAY);
      emit_byte(c, (uint8_t)array_idx);
    }
    
    return;
  }

  // Check if it's a whole array assignment (spectrum = read(SPECTRUM))
  // Need to peek ahead to see if this is "identifier = read(...)"
  if (check(c, TOKEN_EQUAL)) {
    // Save position to potentially backtrack
    token_t saved_current = c->current;
    advance(c); // consume '='

    // Check if right side is read(SPECTRUM) or read(ACCEL)
    if (check(c, TOKEN_IDENTIFIER) && identifier_equals_string(&c->current, "read")) {
      advance(c); // consume 'read'

      if (check(c, TOKEN_LEFT_PAREN)) {
        consume(c, TOKEN_LEFT_PAREN, "Expect '('");
        token_t arg = c->current;
        consume(c, TOKEN_IDENTIFIER, "Expect argument");

        if (identifier_equals_string(&arg, "SPECTRUM")) {
          int8_t array_idx = find_array(c, &target);
          if (array_idx < 0) {
            error(c, "Undefined array");
            return;
          }
          emit_opcode(c, OP_READ_SPECTRUM);
          emit_byte(c, (uint8_t)array_idx);
          consume(c, TOKEN_RIGHT_PAREN, "Expect ')'");
          return;
        } else if (identifier_equals_string(&arg, "ACCEL")) {
          emit_opcode(c, OP_READ_ACCEL);
          consume(c, TOKEN_RIGHT_PAREN, "Expect ')'");
          return;
        }
      }
    }

    // Not a read() call, treat as scalar assignment
    int8_t scalar_idx = find_scalar(c, &target);
    if (scalar_idx < 0) {
      error(c, "Undefined variable");
      return;
    }

    expression(c);

    // Check for "every" temporal clause
    if (match(c, TOKEN_EVERY)) {
    if (!check(c, TOKEN_NUMBER)) {
      error(c, "Expect number after 'every'");
      return;
    }
    advance(c);
    int32_t interval_ms = c->previous.int_value;
    
    if (!match(c, TOKEN_IDENTIFIER) || !identifier_equals_string(&c->previous, "ms")) {
      error(c, "Expect 'ms' after interval");
      return;
    }
    
    // Allocate temporal scalar
    char temporal_name[BOTTLE_NAME_LEN];
    snprintf(temporal_name, BOTTLE_NAME_LEN, "_tick_%d", c->temporal_count);
    token_t temporal_token;
    temporal_token.start = temporal_name;
    temporal_token.length = strlen(temporal_name);
    uint8_t temporal_idx = add_scalar(c, &temporal_token);
    
    bottle_scalar_def_t* scalar = &c->program->scalars[c->program->scalar_count++];
    strncpy(scalar->name, temporal_name, BOTTLE_NAME_LEN);
    scalar->initial_value = 0.0f;
    
    c->temporal_count++;
    
    // Generate conditional
    emit_opcode(c, OP_CALL_MILLIS);
    emit_opcode(c, OP_PUSH_SCALAR);
    emit_byte(c, temporal_idx);
    emit_opcode(c, OP_SUB);
    emit_constant(c, bottle_int(interval_ms));
    emit_opcode(c, OP_GE);
    
    uint16_t skip_jump = emit_jump(c, OP_JUMP_IF_FALSE);

    // Pop the condition (true branch - we're executing the assignment)
    emit_opcode(c, OP_POP);

    emit_opcode(c, OP_CALL_MILLIS);
    emit_opcode(c, OP_POP_SCALAR);
    emit_byte(c, temporal_idx);

    emit_opcode(c, OP_POP_SCALAR);
    emit_byte(c, (uint8_t)scalar_idx);

    // Skip over the false branch's cleanup
    uint16_t end_jump = emit_jump(c, OP_JUMP);

    patch_jump(c, skip_jump);
    // Pop the condition (false branch - we're skipping the assignment)
    emit_opcode(c, OP_POP);
    // Also pop the value that wasn't consumed
    emit_opcode(c, OP_POP);

    patch_jump(c, end_jump);
  } else {
    emit_opcode(c, OP_POP_SCALAR);
    emit_byte(c, (uint8_t)scalar_idx);
  }
  } // End of if (check(c, TOKEN_EQUAL))
}

static void if_statement(compiler_t* c) {
  expression(c);
  
  consume(c, TOKEN_LEFT_BRACE, "Expect '{' after if condition");
  
  uint16_t then_jump = emit_jump(c, OP_JUMP_IF_FALSE);
  emit_opcode(c, OP_POP); // Pop condition
  
  block(c);
  
  uint16_t else_jump = emit_jump(c, OP_JUMP);
  
  patch_jump(c, then_jump);
  emit_opcode(c, OP_POP); // Pop condition
  
  if (match(c, TOKEN_ELSE)) {
    if (match(c, TOKEN_IF)) {
      // else if
      if_statement(c);
    } else {
      // else
      consume(c, TOKEN_LEFT_BRACE, "Expect '{' after else");
      block(c);
    }
  }
  
  patch_jump(c, else_jump);
}

static void for_statement(compiler_t* c) {
  token_t iterator = c->current;
  consume(c, TOKEN_IDENTIFIER, "Expect iterator variable");

  consume(c, TOKEN_IN, "Expect 'in' after iterator");

  // Check if it's range(start, end) or an array
  uint8_t loop_start_value = 0;
  uint8_t loop_end_value = 0;
  bool is_range_loop = false;
  int8_t array_idx = -1;

  if (check(c, TOKEN_IDENTIFIER) && identifier_equals_string(&c->current, "range")) {
    // Parse range(start, end)
    Serial.println("[Bottle] for_statement: parsing range()");
    advance(c); // consume 'range'
    consume(c, TOKEN_LEFT_PAREN, "Expect '(' after 'range'");

    // Parse start value
    if (check(c, TOKEN_NUMBER)) {
      advance(c);
      loop_start_value = (uint8_t)c->previous.int_value;
      Serial.printf("[Bottle] for_statement: range start = %d\n", loop_start_value);
    } else {
      error(c, "Expect number for range start");
      return;
    }

    consume(c, TOKEN_COMMA, "Expect ',' in range");

    // Parse end value (can be number or constant like HEIGHT)
    if (check(c, TOKEN_NUMBER)) {
      advance(c);
      loop_end_value = (uint8_t)c->previous.int_value;
      Serial.printf("[Bottle] for_statement: range end = %d (number)\n", loop_end_value);
    } else if (check(c, TOKEN_IDENTIFIER)) {
      token_t const_name = c->current;
      advance(c);
      if (identifier_equals_string(&const_name, "WIDTH")) {
        loop_end_value = MATRIX_WIDTH;
        Serial.printf("[Bottle] for_statement: range end = WIDTH (%d)\n", loop_end_value);
      } else if (identifier_equals_string(&const_name, "HEIGHT")) {
        loop_end_value = MATRIX_HEIGHT;
        Serial.printf("[Bottle] for_statement: range end = HEIGHT (%d)\n", loop_end_value);
      } else {
        Serial.println("[Bottle] for_statement: ERROR - unknown constant in range");
        error(c, "Unknown constant in range");
        return;
      }
    } else {
      Serial.println("[Bottle] for_statement: ERROR - expect number or constant for range end");
      error(c, "Expect number or constant for range end");
      return;
    }

    consume(c, TOKEN_RIGHT_PAREN, "Expect ')' after range");
    Serial.println("[Bottle] for_statement: range() parsed successfully");
    is_range_loop = true;
  } else if (check(c, TOKEN_IDENTIFIER)) {
    // Try to find as array
    token_t array_name = c->current;
    advance(c);
    array_idx = find_array(c, &array_name);
    if (array_idx < 0) {
      error(c, "Undefined array");
      return;
    }
    loop_start_value = 0;
    loop_end_value = c->program->arrays[array_idx].length;
  } else {
    error(c, "Expect array or range() after 'in'");
    return;
  }

  Serial.printf("[Bottle] for_statement: about to consume '{', current token = %d\n", c->current.type);
  consume(c, TOKEN_LEFT_BRACE, "Expect '{' after for header");
  Serial.println("[Bottle] for_statement: '{' consumed, entering loop body");

  // Save loop context
  bool prev_active = c->loop_context.active;
  char prev_iterator[BOTTLE_NAME_LEN];
  uint8_t prev_array_idx = c->loop_context.array_index;
  uint8_t prev_iterator_scalar_idx = c->loop_context.iterator_scalar_index;
  if (prev_active) {
    strncpy(prev_iterator, c->loop_context.iterator_name, BOTTLE_NAME_LEN);
  }

  c->loop_context.active = true;
  copy_identifier(c->loop_context.iterator_name, &iterator, BOTTLE_NAME_LEN);
  c->loop_context.array_index = is_range_loop ? 255 : array_idx; // 255 means range loop

  // Allocate a scalar for the loop iterator (reuse if already exists)
  token_t iter_token = iterator;
  int8_t existing_idx = find_scalar(c, &iter_token);
  uint8_t iter_idx;
  if (existing_idx >= 0) {
    iter_idx = (uint8_t)existing_idx;
    Serial.printf("[COMPILER] for_statement: reusing scalar[%d] for iterator '%.*s'\n",
                  iter_idx, iterator.length, iterator.start);
  } else {
    iter_idx = add_scalar(c, &iter_token);
    Serial.printf("[COMPILER] for_statement: allocated scalar[%d] for iterator '%.*s'\n",
                  iter_idx, iterator.length, iterator.start);

    // Add to program scalars only if we allocated a new one
    if (c->program->scalar_count >= BOTTLE_MAX_SCALARS) {
      error(c, "Too many scalars");
      return;
    }
    bottle_scalar_def_t* scalar = &c->program->scalars[c->program->scalar_count++];
    copy_identifier(scalar->name, &iterator, BOTTLE_NAME_LEN);
    scalar->initial_value = 0.0f;
  }
  c->loop_context.iterator_scalar_index = iter_idx;

  // Generate runtime loop using bytecode
  // for (i = start; i < end; i++) { body }

  // i = start
  emit_constant(c, bottle_int(loop_start_value));
  emit_opcode(c, OP_POP_SCALAR);
  emit_byte(c, iter_idx);

  uint16_t loop_start_offset = current_offset(c);

  // Check: i < end
  emit_opcode(c, OP_PUSH_SCALAR);
  emit_byte(c, iter_idx);
  emit_constant(c, bottle_int(loop_end_value));
  emit_opcode(c, OP_LT);

  uint16_t exit_jump = emit_jump(c, OP_JUMP_IF_FALSE);
  emit_opcode(c, OP_POP); // Pop condition

  // Parse loop body
  while (!check(c, TOKEN_RIGHT_BRACE) && !check(c, TOKEN_EOF)) {
    statement(c);
  }

  consume(c, TOKEN_RIGHT_BRACE, "Expect '}' after for body");

  // i = i + 1
  emit_opcode(c, OP_PUSH_SCALAR);
  emit_byte(c, iter_idx);
  emit_constant(c, bottle_int(1));
  emit_opcode(c, OP_ADD);
  emit_opcode(c, OP_POP_SCALAR);
  emit_byte(c, iter_idx);

  // Jump back to loop start (relative backward jump)
  emit_opcode(c, OP_JUMP);
  // When VM executes OP_JUMP, PC will be AFTER the u16 offset (i.e., bytecode_size + 2)
  // VM does: pc = pc + offset, so we want: (bytecode_size + 2) + offset = loop_start_offset
  // Therefore: offset = loop_start_offset - (bytecode_size + 2)
  uint16_t jump_from = c->program->bytecode_size + 2;  // PC position after reading the offset
  int16_t backward_offset = (int16_t)(loop_start_offset - jump_from);

  Serial.printf("[COMPILER] for-loop backward jump: loop_start=%d, jump_from=%d, offset=%d\n",
                loop_start_offset, jump_from, backward_offset);

  emit_u16(c, (uint16_t)backward_offset);  // Relative backward jump (negative)

  // Exit point
  patch_jump(c, exit_jump);
  emit_opcode(c, OP_POP); // Pop condition

  // Restore loop context
  c->loop_context.active = prev_active;
  if (prev_active) {
    strncpy(c->loop_context.iterator_name, prev_iterator, BOTTLE_NAME_LEN);
    c->loop_context.array_index = prev_array_idx;
    c->loop_context.iterator_scalar_index = prev_iterator_scalar_idx;
  }
}

static void call_statement(compiler_t* c) {
  token_t func_name = c->previous;
  
  consume(c, TOKEN_LEFT_PAREN, "Expect '(' after function name");
  
  if (identifier_equals_string(&func_name, "clear")) {
    token_t arg = c->current;
    consume(c, TOKEN_IDENTIFIER, "Expect argument");
    if (identifier_equals_string(&arg, "LEDS")) {
      emit_opcode(c, OP_CLEAR_LEDS);
    }
  } else if (identifier_equals_string(&func_name, "show")) {
    token_t arg = c->current;
    consume(c, TOKEN_IDENTIFIER, "Expect argument");
    if (identifier_equals_string(&arg, "LEDS")) {
      emit_opcode(c, OP_SHOW_LEDS);
    }
  } else if (identifier_equals_string(&func_name, "print")) {
    expression(c);
    emit_opcode(c, OP_PRINT);
  } else if (identifier_equals_string(&func_name, "read")) {
    token_t arg = c->current;
    consume(c, TOKEN_IDENTIFIER, "Expect argument");
    if (identifier_equals_string(&arg, "SPECTRUM")) {
      // This should be part of an assignment: spectrum = read(SPECTRUM)
      // For now, just emit the opcode
      emit_opcode(c, OP_READ_SPECTRUM);
      emit_byte(c, 0); // Array index will be set by assignment
    } else if (identifier_equals_string(&arg, "ACCEL")) {
      emit_opcode(c, OP_READ_ACCEL);
    }
  } else if (identifier_equals_string(&func_name, "use")) {
    // Ignore use statements (they're just declarations)
    while (!check(c, TOKEN_RIGHT_PAREN) && !check(c, TOKEN_EOF)) {
      advance(c);
    }
  } else {
    error(c, "Unknown statement function");
  }
  
  consume(c, TOKEN_RIGHT_PAREN, "Expect ')' after arguments");
}

static void leds_assignment(compiler_t* c) {
  // LEDS[x, y] = color
  consume(c, TOKEN_LEFT_BRACKET, "Expect '['");
  expression(c); // x
  consume(c, TOKEN_COMMA, "Expect ','");
  expression(c); // y
  consume(c, TOKEN_RIGHT_BRACKET, "Expect ']'");
  consume(c, TOKEN_EQUAL, "Expect '='");
  expression(c); // color
  
  emit_opcode(c, OP_SET_LED);
}

static void statement(compiler_t* c) {
  char token_text[32];
  int len = min(c->current.length, (uint16_t)31);
  strncpy(token_text, c->current.start, len);
  token_text[len] = '\0';
  Serial.printf("[Bottle] Statement: token=%d '%s' at line %d col %d\n",
                c->current.type, token_text, c->current.line, c->current.column);

  if (match(c, TOKEN_IF)) {
    Serial.println("[Bottle] -> if_statement");
    if_statement(c);
  } else if (match(c, TOKEN_FOR)) {
    Serial.println("[Bottle] -> for_statement");
    for_statement(c);
  } else if (check(c, TOKEN_IDENTIFIER)) {
    token_t name = c->current;
    advance(c);

    if (identifier_equals_string(&name, "LEDS") && check(c, TOKEN_LEFT_BRACKET)) {
      Serial.println("[Bottle] -> leds_assignment");
      leds_assignment(c);
    } else if (check(c, TOKEN_LEFT_PAREN)) {
      Serial.println("[Bottle] -> call_statement");
      call_statement(c);
    } else if (check(c, TOKEN_EQUAL) || check(c, TOKEN_LEFT_BRACKET)) {
      Serial.println("[Bottle] -> assignment_statement");
      c->previous = name;
      assignment_statement(c);
    } else {
      error(c, "Unexpected identifier");
    }
  } else {
    Serial.printf("[Bottle] Unrecognized token %d '%s', skipping\n", c->current.type, token_text);
    error_at_current(c, "Expect statement");
    advance(c); // Skip the unrecognized token to avoid infinite loop
  }
}

static void block(compiler_t* c) {
  Serial.println("[Bottle] Entering block");
  int stmt_count = 0;
  while (!check(c, TOKEN_RIGHT_BRACE) && !check(c, TOKEN_EOF)) {
    statement(c);
    stmt_count++;
    if (stmt_count % 5 == 0) {
      Serial.printf("[Bottle] Block: %d statements parsed\n", stmt_count);
    }
    // Exit if error occurred to prevent infinite loop
    if (c->program->error.has_error) {
      Serial.println("[Bottle] Block: error detected, exiting");
      return;
    }
  }
  Serial.printf("[Bottle] Block complete: %d statements\n", stmt_count);

  consume(c, TOKEN_RIGHT_BRACE, "Expect '}' after block");
}

// Top-level declarations
static void state_declaration(compiler_t* c) {
  token_t name = c->current;
  consume(c, TOKEN_IDENTIFIER, "Expect variable name");
  
  // Check for array declaration
  if (match(c, TOKEN_LEFT_BRACKET)) {
    token_t size_token = c->current;
    
    uint8_t length = MATRIX_WIDTH;
    if (check(c, TOKEN_IDENTIFIER)) {
      advance(c);
      if (identifier_equals_string(&size_token, "WIDTH")) {
        length = MATRIX_WIDTH;
      } else if (identifier_equals_string(&size_token, "HEIGHT")) {
        length = MATRIX_HEIGHT;
      } else {
        error(c, "Unknown array size constant");
        return;
      }
    } else if (check(c, TOKEN_NUMBER)) {
      advance(c);
      length = (uint8_t)c->previous.int_value;
    } else {
      error(c, "Expect array size");
      return;
    }
    
    consume(c, TOKEN_RIGHT_BRACKET, "Expect ']'");
    
    uint8_t initial_value = 0;
    if (match(c, TOKEN_EQUAL)) {
      if (!check(c, TOKEN_NUMBER)) {
        error(c, "Expect number for initial value");
        return;
      }
      advance(c);
      initial_value = (uint8_t)c->previous.int_value;
    }
    
    // Add to compiler symbol table
    uint8_t idx = add_array(c, &name);
    
    // Add to program metadata
    if (c->program->array_count >= BOTTLE_MAX_ARRAYS) {
      error(c, "Too many arrays");
      return;
    }
    bottle_array_def_t* array = &c->program->arrays[c->program->array_count++];
    copy_identifier(array->name, &name, BOTTLE_NAME_LEN);
    array->length = length;
    array->initial_value = initial_value;
    
  } else {
    // Scalar declaration
    float initial_value = 0.0f;
    if (match(c, TOKEN_EQUAL)) {
      if (!check(c, TOKEN_NUMBER)) {
        error(c, "Expect number for initial value");
        return;
      }
      advance(c);
      initial_value = c->previous.is_float ? c->previous.float_value : (float)c->previous.int_value;
    }
    
    // Add to compiler symbol table
    uint8_t idx = add_scalar(c, &name);
    
    // Add to program metadata
    if (c->program->scalar_count >= BOTTLE_MAX_SCALARS) {
      error(c, "Too many scalars");
      return;
    }
    bottle_scalar_def_t* scalar = &c->program->scalars[c->program->scalar_count++];
    copy_identifier(scalar->name, &name, BOTTLE_NAME_LEN);
    scalar->initial_value = initial_value;
  }
}

static void config_declaration(compiler_t* c) {
  token_t key = c->current;
  consume(c, TOKEN_IDENTIFIER, "Expect config key");
  
  consume(c, TOKEN_LEFT_BRACE, "Expect '{' after config key");
  
  if (c->program->config_count >= BOTTLE_MAX_CONFIGS) {
    error(c, "Too many configs");
    return;
  }
  
  bottle_config_def_t* config = &c->program->configs[c->program->config_count++];
  memset(config, 0, sizeof(*config));
  copy_identifier(config->key, &key, BOTTLE_NAME_LEN);
  copy_identifier(config->label, &key, sizeof(config->label));
  config->type = 1; // select
  
  while (!check(c, TOKEN_RIGHT_BRACE) && !check(c, TOKEN_EOF)) {
    if (match(c, TOKEN_TYPE)) {
      token_t type_name = c->current;
      consume(c, TOKEN_IDENTIFIER, "Expect type name");
      if (identifier_equals_string(&type_name, "select")) {
        config->type = 1;
      }
    } else if (match(c, TOKEN_LABEL)) {
      if (check(c, TOKEN_STRING)) {
        advance(c);
        size_t len = min((size_t)(c->previous.length - 2), sizeof(config->label) - 1);
        memcpy(config->label, c->previous.start + 1, len);
        config->label[len] = '\0';
      } else if (check(c, TOKEN_IDENTIFIER)) {
        advance(c);
        copy_identifier(config->label, &c->previous, sizeof(config->label));
      }
    } else if (match(c, TOKEN_DEFAULT)) {
      if (!check(c, TOKEN_NUMBER)) {
        error(c, "Expect number for default value");
        return;
      }
      advance(c);
      config->default_value = (int16_t)c->previous.int_value;
    } else if (match(c, TOKEN_OPTIONS)) {
      if (!check(c, TOKEN_STRING)) {
        error(c, "Expect string for options");
        return;
      }
      advance(c);
      size_t len = min((size_t)(c->previous.length - 2), sizeof(config->options) - 1);
      memcpy(config->options, c->previous.start + 1, len);
      config->options[len] = '\0';
      
      // Count options (separated by |)
      config->max_value = 0;
      for (size_t i = 0; i < len; i++) {
        if (config->options[i] == '|') config->max_value++;
      }
    } else {
      advance(c); // Skip unknown tokens
    }
  }
  
  consume(c, TOKEN_RIGHT_BRACE, "Expect '}' after config body");
}

static void frame_ms_declaration(compiler_t* c) {
  if (!check(c, TOKEN_NUMBER)) {
    error(c, "Expect number after frame_ms");
    return;
  }
  advance(c);
  c->program->frame_ms = (uint16_t)c->previous.int_value;
}

static void setup_block(compiler_t* c) {
  consume(c, TOKEN_LEFT_BRACE, "Expect '{' after setup");
  
  c->program->has_setup = true;
  c->program->setup_offset = current_offset(c);
  
  block(c);
  
  emit_opcode(c, OP_HALT);
}

static void loop_block(compiler_t* c) {
  consume(c, TOKEN_LEFT_BRACE, "Expect '{' after loop");
  
  c->program->has_loop = true;
  c->program->loop_offset = current_offset(c);
  
  block(c);
  
  emit_opcode(c, OP_HALT);
}

static void unload_block(compiler_t* c) {
  consume(c, TOKEN_LEFT_BRACE, "Expect '{' after unload");
  
  c->program->has_unload = true;
  c->program->unload_offset = current_offset(c);
  
  block(c);
  
  emit_opcode(c, OP_HALT);
}

static void skip_line(compiler_t* c) {
  while (!check(c, TOKEN_EOF) && c->current.line == c->previous.line) {
    advance(c);
  }
}

static void declaration(compiler_t* c) {
  token_type_t current_token = c->current.type;

  if (match(c, TOKEN_RUNTIME)) {
    skip_line(c);
  } else if (match(c, TOKEN_MODULE)) {
    skip_line(c);
  } else if (match(c, TOKEN_REQUIRE)) {
    skip_line(c);
  } else if (match(c, TOKEN_USE)) {
    skip_line(c);
  } else if (match(c, TOKEN_STATE)) {
    state_declaration(c);
  } else if (match(c, TOKEN_CONFIG)) {
    config_declaration(c);
  } else if (match(c, TOKEN_FRAME_MS)) {
    frame_ms_declaration(c);
  } else if (match(c, TOKEN_SETUP)) {
    setup_block(c);
  } else if (match(c, TOKEN_LOOP)) {
    loop_block(c);
  } else if (match(c, TOKEN_UNLOAD)) {
    unload_block(c);
  } else {
    Serial.printf("[Bottle] Unknown token type: %d\n", current_token);
    error_at_current(c, "Expect declaration");
    advance(c);
  }
}

// Main compilation function
bool bottle_compile(const char* script_text, bottle_program_t* out_program) {
  if (!script_text || !out_program) return false;

  // Debug: print script header
  Serial.printf("[Bottle] Compiling script (%d bytes)...\n", strlen(script_text));

  // Initialize program
  memset(out_program, 0, sizeof(*out_program));
  bottle_error_init(&out_program->error);
  out_program->frame_ms = 33; // Default 30 FPS

  // Initialize compiler
  compiler_t compiler;
  memset(&compiler, 0, sizeof(compiler));
  compiler.program = out_program;

  init_tokenizer(&compiler.tokenizer, script_text);

  // Prime the pump
  advance(&compiler);

  // Parse declarations
  while (!match(&compiler, TOKEN_EOF)) {
    declaration(&compiler);

    if (out_program->error.has_error) {
      bottle_error_print(&out_program->error, "Compile");
      return false;
    }
  }

  // Validate program
  if (!out_program->has_loop) {
    bottle_error_set(&out_program->error, 0, 0, "Program must have a loop block");
    bottle_error_print(&out_program->error, "Compile");
    return false;
  }
  
  // Success
  Serial.printf("[Bottle] Compiled successfully: %d bytes bytecode, %d constants, %d arrays, %d scalars\n",
                out_program->bytecode_size, out_program->constant_count,
                out_program->array_count, out_program->scalar_count);
  Serial.printf("[Bottle] Entry points: setup=%d, loop=%d, unload=%d\n",
                out_program->setup_offset, out_program->loop_offset, out_program->unload_offset);

  return true;
}

// Opcode name lookup (for debugging)
const char* bottle_opcode_name(bottle_opcode_t op) {
  switch (op) {
    case OP_PUSH_CONST: return "PUSH_CONST";
    case OP_PUSH_SCALAR: return "PUSH_SCALAR";
    case OP_PUSH_ARRAY: return "PUSH_ARRAY";
    case OP_POP_SCALAR: return "POP_SCALAR";
    case OP_POP_ARRAY: return "POP_ARRAY";
    case OP_DUP: return "DUP";
    case OP_POP: return "POP";
    case OP_ADD: return "ADD";
    case OP_SUB: return "SUB";
    case OP_MUL: return "MUL";
    case OP_DIV: return "DIV";
    case OP_MOD: return "MOD";
    case OP_NEG: return "NEG";
    case OP_LT: return "LT";
    case OP_LE: return "LE";
    case OP_GT: return "GT";
    case OP_GE: return "GE";
    case OP_EQ: return "EQ";
    case OP_NE: return "NE";
    case OP_AND: return "AND";
    case OP_OR: return "OR";
    case OP_NOT: return "NOT";
    case OP_JUMP: return "JUMP";
    case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
    case OP_CALL_MAX: return "CALL_MAX";
    case OP_CALL_MIN: return "CALL_MIN";
    case OP_CALL_CLAMP: return "CALL_CLAMP";
    case OP_CALL_ABS: return "CALL_ABS";
    case OP_CALL_SQRT: return "CALL_SQRT";
    case OP_CALL_SIN: return "CALL_SIN";
    case OP_CALL_COS: return "CALL_COS";
    case OP_CALL_RANDOM: return "CALL_RANDOM";
    case OP_CALL_MILLIS: return "CALL_MILLIS";
    case OP_CALL_HSV: return "CALL_HSV";
    case OP_CALL_RGB: return "CALL_RGB";
    case OP_CALL_BLEND: return "CALL_BLEND";
    case OP_READ_SPECTRUM: return "READ_SPECTRUM";
    case OP_READ_ACCEL: return "READ_ACCEL";
    case OP_CLEAR_LEDS: return "CLEAR_LEDS";
    case OP_SET_LED: return "SET_LED";
    case OP_SHOW_LEDS: return "SHOW_LEDS";
    case OP_PRINT: return "PRINT";
    case OP_HALT: return "HALT";
    default: return "UNKNOWN";
  }
}
