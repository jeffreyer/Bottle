#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

// Mock ESP32 constants
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16

// Include the Bottle VM headers (we'll need to adapt them)
#define BOTTLE_NAME_LEN 32
#define BOTTLE_MAX_ARRAYS 8
#define BOTTLE_MAX_SCALARS 16
#define BOTTLE_MAX_STATES 8
#define BOTTLE_MAX_CODE 2048
#define BOTTLE_MAX_CONSTANTS 32

// Token types (from bottle_compiler.cpp)
typedef enum {
  TOKEN_LEFT_PAREN = 0, TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
  TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
  TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
  TOKEN_PERCENT,
  TOKEN_BANG, TOKEN_BANG_EQUAL,
  TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER, TOKEN_GREATER_EQUAL,
  TOKEN_LESS, TOKEN_LESS_EQUAL,
  TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
  TOKEN_AND, TOKEN_ELSE, TOKEN_FALSE,
  TOKEN_FOR, TOKEN_IF, TOKEN_IN,
  TOKEN_OR, TOKEN_TRUE,
  TOKEN_RUNTIME, TOKEN_MODULE,
  TOKEN_STATE, TOKEN_CONFIG, TOKEN_SETUP, TOKEN_LOOP,
  TOKEN_ERROR, TOKEN_EOF
} token_type_t;

typedef struct {
  token_type_t type;
  const char* start;
  uint16_t length;
  uint16_t line;
  int int_value;
} token_t;

typedef struct {
  const char* start;
  const char* current;
  uint16_t line;
} tokenizer_t;

typedef struct {
  uint8_t length;
  char name[BOTTLE_NAME_LEN];
} array_t;

typedef struct {
  char name[BOTTLE_NAME_LEN];
} scalar_t;

typedef struct {
  char name[BOTTLE_NAME_LEN];
  uint8_t value;
} state_t;

typedef struct {
  uint8_t code[BOTTLE_MAX_CODE];
  uint16_t code_count;
  array_t arrays[BOTTLE_MAX_ARRAYS];
  uint8_t array_count;
  scalar_t scalars[BOTTLE_MAX_SCALARS];
  uint8_t scalar_count;
  state_t states[BOTTLE_MAX_STATES];
  uint8_t state_count;
  uint8_t constants[BOTTLE_MAX_CONSTANTS];
  uint8_t constant_count;
  uint16_t setup_offset;
  uint16_t loop_offset;
  uint8_t frame_ms;
} bottle_program_t;

typedef struct {
  bool active;
  char iterator_name[BOTTLE_NAME_LEN];
  uint8_t array_index;
} loop_context_t;

typedef struct {
  tokenizer_t tokenizer;
  token_t current;
  token_t previous;
  bool had_error;
  bool panic_mode;
  bottle_program_t* program;
  loop_context_t loop_context;
} compiler_t;

// Forward declarations
static void advance(compiler_t* c);
static bool check(compiler_t* c, token_type_t type);
static bool match(compiler_t* c, token_type_t type);
static void consume(compiler_t* c, token_type_t type, const char* message);
static void error(compiler_t* c, const char* message);
static void declaration(compiler_t* c);
static void statement(compiler_t* c);
static void block(compiler_t* c);

// Tokenizer implementation
static char peek(tokenizer_t* t) {
  return *t->current;
}

static char peek_next(tokenizer_t* t) {
  if (*t->current == '\0') return '\0';
  return t->current[1];
}

static char advance_char(tokenizer_t* t) {
  t->current++;
  return t->current[-1];
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
        advance_char(t);
        break;
      case '/':
        if (peek_next(t) == '/') {
          while (peek(t) != '\n' && peek(t) != '\0') advance_char(t);
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
  token.int_value = 0;
  return token;
}

static token_t error_token(tokenizer_t* t, const char* message) {
  token_t token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (uint16_t)strlen(message);
  token.line = t->line;
  token.int_value = 0;
  return token;
}

static bool identifier_equals(tokenizer_t* t, const char* keyword) {
  size_t len = strlen(keyword);
  return (size_t)(t->current - t->start) == len &&
         memcmp(t->start, keyword, len) == 0;
}

static token_type_t check_keyword(tokenizer_t* t) {
  if (identifier_equals(t, "and")) return TOKEN_AND;
  if (identifier_equals(t, "else")) return TOKEN_ELSE;
  if (identifier_equals(t, "false")) return TOKEN_FALSE;
  if (identifier_equals(t, "for")) return TOKEN_FOR;
  if (identifier_equals(t, "if")) return TOKEN_IF;
  if (identifier_equals(t, "in")) return TOKEN_IN;
  if (identifier_equals(t, "or")) return TOKEN_OR;
  if (identifier_equals(t, "true")) return TOKEN_TRUE;
  if (identifier_equals(t, "runtime")) return TOKEN_RUNTIME;
  if (identifier_equals(t, "module")) return TOKEN_MODULE;
  if (identifier_equals(t, "state")) return TOKEN_STATE;
  if (identifier_equals(t, "config")) return TOKEN_CONFIG;
  if (identifier_equals(t, "setup")) return TOKEN_SETUP;
  if (identifier_equals(t, "loop")) return TOKEN_LOOP;
  return TOKEN_IDENTIFIER;
}

static token_t identifier(tokenizer_t* t) {
  while (isalnum((unsigned char)peek(t)) || peek(t) == '_' || peek(t) == '-' || peek(t) == '.' || peek(t) == '@') {
    advance_char(t);
  }
  return make_token(t, check_keyword(t));
}

static token_t number(tokenizer_t* t) {
  while (isdigit((unsigned char)peek(t))) advance_char(t);

  token_t token = make_token(t, TOKEN_NUMBER);
  char buffer[32];
  size_t len = token.length < 31 ? token.length : 31;
  memcpy(buffer, token.start, len);
  buffer[len] = '\0';
  token.int_value = atoi(buffer);
  return token;
}

static token_t scan_token(tokenizer_t* t) {
  skip_whitespace(t);
  t->start = t->current;

  if (*t->current == '\0') return make_token(t, TOKEN_EOF);

  char c = advance_char(t);

  if (isalpha((unsigned char)c) || c == '_') return identifier(t);
  if (isdigit((unsigned char)c)) return number(t);

  switch (c) {
    case '(': return make_token(t, TOKEN_LEFT_PAREN);
    case ')': return make_token(t, TOKEN_RIGHT_PAREN);
    case '{': return make_token(t, TOKEN_LEFT_BRACE);
    case '}': return make_token(t, TOKEN_RIGHT_BRACE);
    case '[': return make_token(t, TOKEN_LEFT_BRACKET);
    case ']': return make_token(t, TOKEN_RIGHT_BRACKET);
    case ',': return make_token(t, TOKEN_COMMA);
    case '.': return make_token(t, TOKEN_DOT);
    case '-': return make_token(t, TOKEN_MINUS);
    case '+': return make_token(t, TOKEN_PLUS);
    case ';': return make_token(t, TOKEN_SEMICOLON);
    case '/': return make_token(t, TOKEN_SLASH);
    case '*': return make_token(t, TOKEN_STAR);
    case '%': return make_token(t, TOKEN_PERCENT);
    case '!':
      return make_token(t, peek(t) == '=' ? (advance_char(t), TOKEN_BANG_EQUAL) : TOKEN_BANG);
    case '=':
      return make_token(t, peek(t) == '=' ? (advance_char(t), TOKEN_EQUAL_EQUAL) : TOKEN_EQUAL);
    case '<':
      return make_token(t, peek(t) == '=' ? (advance_char(t), TOKEN_LESS_EQUAL) : TOKEN_LESS);
    case '>':
      return make_token(t, peek(t) == '=' ? (advance_char(t), TOKEN_GREATER_EQUAL) : TOKEN_GREATER);
  }

  return error_token(t, "Unexpected character");
}

// Compiler helper functions
static void advance(compiler_t* c) {
  c->previous = c->current;

  for (;;) {
    c->current = scan_token(&c->tokenizer);
    if (c->current.type != TOKEN_ERROR) break;

    error(c, c->current.start);
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
  error(c, message);
}

static void error(compiler_t* c, const char* message) {
  if (c->panic_mode) return;
  c->panic_mode = true;
  c->had_error = true;

  printf("[Bottle] Error at line %d: %s\n", c->current.line, message);
  if (c->current.type == TOKEN_IDENTIFIER || c->current.type == TOKEN_NUMBER) {
    printf("  Token: '%.*s'\n", c->current.length, c->current.start);
  }
}

static bool identifier_equals_string(token_t* token, const char* str) {
  size_t len = strlen(str);
  return token->length == len && memcmp(token->start, str, len) == 0;
}

static void copy_identifier(char* dest, token_t* token, size_t max_len) {
  size_t len = token->length < max_len - 1 ? token->length : max_len - 1;
  memcpy(dest, token->start, len);
  dest[len] = '\0';
}

// Stub implementations for now
static void declaration(compiler_t* c) {
  printf("[Bottle] declaration() called, current token type = %d\n", c->current.type);

  if (match(c, TOKEN_RUNTIME)) {
    printf("[Bottle] Parsing runtime declaration\n");
    consume(c, TOKEN_IDENTIFIER, "Expect runtime identifier");
    return;
  }

  if (match(c, TOKEN_MODULE)) {
    printf("[Bottle] Parsing module declaration\n");
    consume(c, TOKEN_IDENTIFIER, "Expect module identifier");
    return;
  }

  if (match(c, TOKEN_STATE)) {
    printf("[Bottle] Parsing state declaration\n");
    consume(c, TOKEN_IDENTIFIER, "Expect state name");
    // Skip array size if present
    if (match(c, TOKEN_LEFT_BRACKET)) {
      advance(c); // skip size
      consume(c, TOKEN_RIGHT_BRACKET, "Expect ']'");
    }
    return;
  }

  if (match(c, TOKEN_CONFIG)) {
    printf("[Bottle] Parsing config declaration\n");
    consume(c, TOKEN_IDENTIFIER, "Expect config name");
    consume(c, TOKEN_EQUAL, "Expect '='");
    advance(c); // skip value
    return;
  }

  if (match(c, TOKEN_SETUP)) {
    printf("[Bottle] Parsing setup block\n");
    consume(c, TOKEN_LEFT_BRACE, "Expect '{'");
    block(c);
    return;
  }

  if (match(c, TOKEN_LOOP)) {
    printf("[Bottle] Parsing loop block\n");
    consume(c, TOKEN_LEFT_BRACE, "Expect '{'");
    block(c);
    return;
  }

  error(c, "Unexpected declaration");
}

static void statement(compiler_t* c) {
  static int stmt_count = 0;
  stmt_count++;

  if (stmt_count % 5 == 0) {
    printf("[Bottle] Parsed %d statements so far...\n", stmt_count);
  }

  printf("[Bottle] statement() called, token=%d", c->current.type);
  if (c->current.type == TOKEN_IDENTIFIER || c->current.type == TOKEN_NUMBER) {
    printf(" text='%.*s'", (int)c->current.length, c->current.start);
  }
  printf(" at line %d\n", c->current.line);

  if (match(c, TOKEN_IF)) {
    printf("[Bottle] Parsing if statement\n");
    // Skip for now
    return;
  }

  if (match(c, TOKEN_FOR)) {
    printf("[Bottle] Parsing for statement\n");
    consume(c, TOKEN_IDENTIFIER, "Expect iterator variable");
    consume(c, TOKEN_IN, "Expect 'in'");

    // Check for range()
    if (check(c, TOKEN_IDENTIFIER) && identifier_equals_string(&c->current, "range")) {
      printf("[Bottle] for_statement: parsing range()\n");
      advance(c); // consume 'range'
      consume(c, TOKEN_LEFT_PAREN, "Expect '(' after 'range'");

      if (check(c, TOKEN_NUMBER)) {
        advance(c);
        printf("[Bottle] for_statement: range start = %d\n", c->previous.int_value);
      } else {
        error(c, "Expect number for range start");
        return;
      }

      consume(c, TOKEN_COMMA, "Expect ',' in range");

      if (check(c, TOKEN_NUMBER)) {
        advance(c);
        printf("[Bottle] for_statement: range end = %d (number)\n", c->previous.int_value);
      } else if (check(c, TOKEN_IDENTIFIER)) {
        token_t const_name = c->current;
        advance(c);
        if (identifier_equals_string(&const_name, "WIDTH")) {
          printf("[Bottle] for_statement: range end = WIDTH (%d)\n", MATRIX_WIDTH);
        } else if (identifier_equals_string(&const_name, "HEIGHT")) {
          printf("[Bottle] for_statement: range end = HEIGHT (%d)\n", MATRIX_HEIGHT);
        } else {
          printf("[Bottle] for_statement: ERROR - unknown constant in range\n");
          error(c, "Unknown constant in range");
          return;
        }
      } else {
        printf("[Bottle] for_statement: ERROR - expect number or constant for range end\n");
        error(c, "Expect number or constant for range end");
        return;
      }

      consume(c, TOKEN_RIGHT_PAREN, "Expect ')' after range");
      printf("[Bottle] for_statement: range() parsed successfully\n");
    }

    printf("[Bottle] for_statement: about to consume '{', current token = %d\n", c->current.type);
    consume(c, TOKEN_LEFT_BRACE, "Expect '{' after for header");
    printf("[Bottle] for_statement: '{' consumed, entering loop body\n");
    block(c);
    return;
  }

  // Expression statement
  printf("[Bottle] Parsing expression statement\n");
  // Skip tokens until semicolon or end of block
  while (!check(c, TOKEN_SEMICOLON) && !check(c, TOKEN_RIGHT_BRACE) && !check(c, TOKEN_EOF)) {
    advance(c);
  }
  if (match(c, TOKEN_SEMICOLON)) {
    // consumed
  }
}

static void block(compiler_t* c) {
  printf("[Bottle] block() entered\n");
  int stmt_count = 0;
  while (!check(c, TOKEN_RIGHT_BRACE) && !check(c, TOKEN_EOF)) {
    if (c->had_error) {
      printf("[Bottle] block() exiting due to error\n");
      break;
    }
    statement(c);
    stmt_count++;
  }
  consume(c, TOKEN_RIGHT_BRACE, "Expect '}' after block");
  printf("[Bottle] block() completed with %d statements\n", stmt_count);
}

// Main compile function
bool bottle_compile(const char* source, bottle_program_t* program) {
  compiler_t compiler;
  memset(&compiler, 0, sizeof(compiler_t));

  compiler.tokenizer.start = source;
  compiler.tokenizer.current = source;
  compiler.tokenizer.line = 1;
  compiler.program = program;
  compiler.had_error = false;
  compiler.panic_mode = false;

  printf("[Bottle] Compiling script (%zu bytes)...\n", strlen(source));
  printf("[Bottle] First 100 chars:\n");
  for (int i = 0; i < 100 && source[i] != '\0'; i++) {
    if (source[i] >= 32 && source[i] < 127) {
      printf("%c", source[i]);
    } else if (source[i] == '\n') {
      printf("\\n\n");
    } else {
      printf("<%02X>", (unsigned char)source[i]);
    }
  }
  printf("\n\n");

  advance(&compiler);

  int decl_count = 0;
  while (!match(&compiler, TOKEN_EOF)) {
    declaration(&compiler);
    decl_count++;
    if (decl_count % 10 == 0) {
      printf("[Bottle] Parsed %d declarations so far...\n", decl_count);
    }
    if (compiler.had_error) break;
  }

  if (compiler.had_error) {
    printf("[Bottle] Compilation failed\n");
    return false;
  }

  printf("[Bottle] Compilation successful! Parsed %d declarations\n", decl_count);
  return true;
}

// Test script
const char* test_script = R"(
runtime bottle-vm@0.2
module rhythm.spectrum

state spectrum[WIDTH]
state hue
state brightness
state speed
state mode
state last_update

config frame_ms = 50

setup {
  hue = 0;
  brightness = 255;
  speed = 5;
  mode = 0;
  last_update = 0;
}

loop {
  for y in range(0, HEIGHT) {
    spectrum[y] = y * 10;
  }
}
)";

int main() {
  printf("=== Bottle VM Test ===\n\n");

  bottle_program_t program;
  memset(&program, 0, sizeof(bottle_program_t));

  bool success = bottle_compile(test_script, &program);

  if (success) {
    printf("\n=== Compilation SUCCESS ===\n");
    return 0;
  } else {
    printf("\n=== Compilation FAILED ===\n");
    return 1;
  }
}
