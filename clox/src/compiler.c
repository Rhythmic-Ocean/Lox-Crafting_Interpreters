// NOTE: delcaring variable means when it's initliazed in the memory
// defining it means when it's available for usage

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// A local struct for parser
typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode; // to initiate error recovery
} Parser;

// Enum with precedence in order for pratt parsing
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISION,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY,
} Precedence;

// ParseFn is just an alias for a function pointer that returns void and can
// take argument of type bool
typedef void (*ParseFn)(bool canAssign);

// struct that holds the infix and prefix function for any corresponding
// precedence
typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

// just holds the token name and scope depth of a local variable
typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t
      index; // the index of local slot the upvalue is captuing in the enclosing
  bool isLocal; // true if upvalue is from the immediate enclosing, false if not
                // until we reach a function whose immediate enclosing has the
                // required local as upvalue
} Upvalue;

typedef enum {
  TYPE_FUNCTION,
  TYPE_METHOD,
  TYPE_SCRIPT,
} FunctionType;

// a namespace equivalent for local variables, just a names of the locals are
// stored here tho NOTE: Only exists during compile time to emit corresponding
// bytecodes, DOES NOT pass on to the VM
typedef struct Compiler {
  struct Compiler *enclosing;
  ObjFunction *function;
  FunctionType type;
  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalue[UINT8_COUNT];
  int scopeDepth;
} Compiler;

typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;
} ClassCompiler;

Parser parser;
Compiler *current =
    NULL; // always points at the current function's Compiler (look at
          // initCompiler() which runs after every new fnc is introduced)
ClassCompiler *currentClass = NULL;
Chunk *compilingChunk;

/**
 * Returns temporary chunk for the duration of compilation
 *
 * @return chunk*
 */
static Chunk *currentChunk() { return &current->function->chunk; }

/**
 * Given the Token producing error and the error message to be delivered, it
 * prints it in proper format at stderr
 *
 * @param token Token that's at or nearest to error prone location
 * @param message Corresponding string of error message
 *
 * @return void
 *
 * NOTE: When at panicMode, simpley returns as we don't wanna report any more
 * error until the program srynconizes after error
 *
 * If panicMode is not true, toggles it, and prints an appropriate error message
 */
static void errorAt(Token *token, const char *message) {
  if (parser.panicMode)
    return;
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }
  fprintf(stderr, ":%s\n", message);
  parser.hadError = true;
}

static void error(const char *message) { errorAt(&parser.current, message); }

static void errorAtCurrent(const char *message) {
  errorAt(&parser.current, message);
}

/**
 * Advances the parser to next token
 *
 * This function invokes scanToken() until it encounts a non error token type.
 * If an TOKEN_ERROR is thrown, it reports it and tries again.
 * parser.current is assigned the latest token type while parser.previous
 * holds the one right before.
 *
 * @return void
 */
static void advance() {
  parser.previous = parser.current;
  for (;;) {
    parser.current = scanToken();

    if (parser.current.type != TOKEN_ERROR)
      break;

    errorAtCurrent(parser.current.start);
  }
}

/**
 * Returns true and 'advances' if the Token being pointe by parser.current is
 * of the type desired, else error is through at current token with given
 * message
 *
 * @param type the desired TokenType
 * @param message a string of error message, in case the type check is negative
 * @return void
 *
 * Special note: like match but throws error instead of returning false on
 * negative result strict tokentype check
 */
static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }
  errorAtCurrent(message);
}
/**
 * Just checks weather token in parser.current's type is equal to the given
 * TokeType
 *
 * @param type
 * @return bool
 */
static bool check(TokenType type) { return parser.current.type == type; }

/**
 * Returns true and 'advances' if the Token being pointed at by the
 * parser.current is of the type desired, else returns false.
 * @param type a TokenType
 * @return bool
 *
 * Special note: like consume but returns false instead of error type
 * non-strict tokentype check
 */
static bool match(TokenType type) {
  if (!check(type))
    return false;
  advance();
  return true;
}

/**
 * Emits one byte to current Chunk using the writeChunk() function
 *
 * @param byte
 * @return void
 */
static void emitByte(uint8_t byte) { // writing into the chunk
  writeChunk(currentChunk(), byte, parser.previous.line);
}

/**
 * For emitting 2 bytes at once for some bytecode of type OP_CONSTANT or
 * OP_DEFINE_GLOBAL
 *
 * @param byte1 usually OP_CONSTANT, OP_DEFINE_GLOBAL or smthing like that
 * @param byte2 usually index of the corresponding value it represents
 */
static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitLoops(int loopStart) {

  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX)
    error("LOOP body too large");

  emitByte((offset >> 8) & 0xff);
  emitByte((offset & 0xff));
}
/**
 * emits the jumping instruction plus 2 bytes as jump offset operand
 * 2 bytes cuz together they make 16 bits offset letting us jump over 65k+
 * bytes of code!
 *
 * The function returns the offset of the emitted instruction in the chunk
 *
 * @param instruction the jump instruction usually
 *
 * @return int the offset of jump instruction in the bytecode
 */
static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

/**
 * Just invokes emitByte to return a bytecode that's OP_RETURN
 */
static void emitReturn() {
  emitByte(OP_NIL); // if there's nothing to return, the vm will return nil
                    // implicitly
  emitByte(OP_RETURN);
}

/**
 * Given a Value, it invokes addConstant to add it to the ValueArray in the
 * compiler chunk and return the corresponding index where the constant is
 * stored
 *
 * @param value the value to tbe stored in the array
 * @return uint8_t the index where it is stored
 */
static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk");
    return 0;
  }
  return (uint8_t)constant;
}
/**
 * Takes how much the vm needs to jump ahead (the offset value) in case if
 * condition is false and then turns that 16 bit int value to two 8 bits values.
 * Those 8 bits are then patched up in the place where they are supposed to be
 * in, right after OP_JUMP_IF_FALSE
 *
 * @param offset the 16 bit number that needs splitting
 *
 * @return void
 */
static void patchJump(int offset) {
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

/**
 * Emits OP_CONSTANT followed by the corresponding value to the chunk
 *
 * @param value
 * @return void
 */
static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}
/**
 * Initiates the local namespace for the compilation process
 *
 * @param compiler
 *
 * @return void
 */
static void initCompiler(Compiler *compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function =
      newFunction(); // making the function null only to assign stuff to it
                     // immediatly after is for garbage collection apprantly
  current = compiler;
  if (type != TYPE_SCRIPT) {
    current->function->name =
        copyString(parser.previous.start, parser.previous.length);
  }
  /*
  Remember that the compiler’s locals array keeps track of which stack slots are
  associated with which local variables or temporaries. From now on, the
  compiler implicitly claims stack slot zero for the VM’s own internal use. We
  give it an empty name so that the user can’t write an identifier that refers
  to it.
  */
  Local *local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}
/**
 * emits OP_RETURN at the end of compilation to the chunk to indicate end of
 * program
 *
 * @return void
 */

static ObjFunction *endCompiler() {
  emitReturn();
  ObjFunction *function = current->function;
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(
        currentChunk(),
        function->name != NULL
            ? function->name->chars
            : "<script>"); // the second parameter is the name of the function,
                           // if it's global just gives the name <script>
  }
#endif
  current = current->enclosing;
  return function;
}
/**
 * beginning the block's scope by increasing the overall compiler scope
 *
 * @return void
 */
static void beginScope() { current->scopeDepth++; }

/**
 * ending the block's scopeby decreasing the overall compiler's scope plus
 * emitting x numbers of OP_POPs where x is the number of local variables at
 * that scope This interprets to removing all the value that's in the scope
 * that's in the stack after the block for that scope ends
 *
 * @return void
 */
static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    if (current->locals[current->localCount - 1]
            .isCaptured) { // if current variable has been "captured" we give
                           // instruction to hoist it oer to the heap
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}

static void expression();
static void statement();
static void declaration();
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

/**
 * Returns the index of the global variable name in the global ValueArray
 *
 * @return uint8_t
 */

static uint8_t identifierConstant(Token *name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}
/**
 * Returns true or false depending on weather two variable names are equivalent
 * or not
 *
 * @param a First Token
 * @param b Second Token
 * @return bool
 */
static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length)
    return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

/**
 * Searching for given variable name in local namespace and returning it's index
 * in current->local if found returns -1 if not found
 *
 * @param compiler namesapce for local variable
 *
 */
static int resolveLocal(Compiler *compiler, Token *name) {
  for (int i = compiler->localCount - 1; i >= 0;
       i--) { // walking backwards to account for shadowing
    Local *local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local var in it's own initializer!");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue *upvalue = &compiler->upvalue[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalue[upvalueCount].isLocal = isLocal;
  compiler->upvalue[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

// funny recursion, be careful!! postorder + preorder mashed up
static int resolveUpvalue(Compiler *compiler, Token *name) {
  if (compiler->enclosing == NULL)
    return -1;

  int local = resolveLocal(
      compiler->enclosing,
      name); // we first check the immediate outer function for local reference
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(
      compiler->enclosing,
      name); /*if we can't find the local reference in immediate enclosing,
              * we got over the enclosing's enclosing again and again
              * recursively until we reach the end or we find something.
              */
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }
  return -1;
}

/**
 * Creates i.e adds aka declears the local variable by adding it to the Compiler
 * current and creating a new local identifier. Throws error if there's more
 * than UINT8_COUNT local variable (can be changed) local->depth is set to -1 as
 * the variable has not been 'defined' yet!
 */
static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in the function.");
    return;
  }
  Local *local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

/**
 * if the given variable is global just return.
 *
 * For Local variable:
 *  It iterates through every local variable in the the Local array FROM BACK at
 * current->local. The loop breaks when it encounters a local with: local->depth
 * < current scopeDepth --> implies that we are out of current scope, i.e there
 * can't be a variable of same name within the same scope we can safely define
 * this local local->depth != -1: If a local has -1 depth when going thru array
 * of locals it simply  means that we are still inside the current->scopeDepth.
 * We simply encountered a variable within this scope that hasn't been
 * initialized yet (USEFUL ONLY FOR MULTI_VAR DECLARAION, it's not here in the
 * program rn tho)
 *
 *      BUT if there's a local of same name, within the same scope, then an
 * error is thrown.
 *
 * if it passes the loop or breaks out of it, addLocal() is called to finally
 * add map it as a local variable!
 *
 * @return void
 *
 * NOTE: For global vars, it's mapped in hashtables, which is done at runtime.
 * So you just turn the var names to (Value)OBJ_STRING and have it with
 * OP_DEFINE_GLOBAL. Also have the value as Value and store it's index at
 *      ValueArray in the chunk alongside OP_CONST. GLOBAL is decleared like
 * this: so for var a = 1+1*2; OP_CONST INDEX_OF_1 OP_CONST INDEX_OF_1 OP_CONST
 *      INDEX_OF_2
 *      OP_STAR
 *      OP_ADD
 *      OP_DEFINE_GLOBAL
 *      INDEX_OF_a
 *      And when the vm sees OP_DEFINE_GLOBAL a at the end, it maps a with
 * whatever is the result of var declaration. A special note, INDEX_OF_a is
 * usually the lowest in ValueArray, and all these INDEX_OF are uint_8 type, so
 * just numbers
 *
 * NOTE:
 *  var a = 1;
 *  var a = 1;
 *  A double declearation like this only a problem in local scope.
 *  It's allowed in global scope for clox.
 *
 *
 */
static void declareVariable() {
  if (current->scopeDepth == 0)
    return;

  Token *name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local *local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a vairable with this name in scope.");
    }
  }

  addLocal(*name);
}

static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      //> arg-limit
      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }
      //< arg-limit
      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}
/**
 * an and expression is like if else block, for eg, for A and B, it's like
 *  if(A == false){
 *      don't pop A, it's the final result
 *      jump over B
 *  }
 *  else{
 *      pop A, whatever's B is the final result
 * }
 *
 * corresponding bytecode might look like this:
 * 0006     4OP_GET_GLOBAL       2  'a'
 * 0008   |OP_JUMP_IF_FALSE    8 -> 14
 * 0011   |OP_POP
 * 0012   |OP_GET_GLOBAL       3  'b'
 *
 * i.e if the left side is falsey we know the entire expression is false so we
 * just skip
 */
static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule *rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
  case TOKEN_BANG_EQUAL:
    emitBytes(OP_EQUAL, OP_NOT);
    break;
  case TOKEN_EQUAL_EQUAL:
    emitByte(OP_EQUAL);
    break;
  case TOKEN_GREATER:
    emitByte(OP_GREATER);
    break;
  case TOKEN_GREATER_EQUAL:
    emitBytes(OP_LESS, OP_NOT);
    break;
  case TOKEN_LESS:
    emitByte(OP_LESS);
    break;
  case TOKEN_LESS_EQUAL:
    emitBytes(OP_GREATER, OP_NOT);
    break;
  case TOKEN_BANG:
    emitByte(OP_NOT);
    break;
  case TOKEN_PLUS:
    emitByte(OP_ADD);
    break;
  case TOKEN_MINUS:
    emitByte(OP_SUBTRACT);
    break;
  case TOKEN_STAR:
    emitByte(OP_MULTIPLY);
    break;
  case TOKEN_SLASH:
    emitByte(OP_DIVIDE);
    break;
  default:
    return;
  }
}

static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint8_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  } else {
    emitBytes(OP_GET_PROPERTY, name);
  }
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
  case TOKEN_FALSE:
    emitByte(OP_FALSE);
    break;
  case TOKEN_NIL:
    emitByte(OP_NIL);
    break;
  case TOKEN_TRUE:
    emitByte(OP_TRUE);
    break;
  default:
    return; // Unreachable
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void numbers(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}
/**
 * OR is more like normal If for A or B:
 *
 * if(A == true){
 *      Jump over B else, A is the final value
 * }
 * else{
 *      POP A, B is the final value
 * }
 *
 * Corresponding bytecodes might be like this:
 * 0006     4OP_GET_GLOBAL       2  'a'
 * 0008   |OP_JUMP_IF_FALSE    8 -> 14
 * 0011   |         OP_JUMP   11 -> 17
 * 0014   |OP_POP
 * 0015   |OP_GET_GLOBAL       3  'b'
 *
 */
static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}
static void string(bool canAssign) {
  emitConstant(OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
} //+1 and -2 to remove "" and the last \0

/**
 * If resolveLocal returns -1, that means it's GLOBAL variable and corresponding
 * stuffs are emitted else it's local and OP_SET_LOCAL or OP_GET_LOCAL is
 * emitted based on weather the next token is TOKEN_EQUAL or not
 *
 * NOTE: Local and Global Variable WORK very differently even tho when compiler
 * emits similar things when encountering them (during non declearation phase)
 *       It starts from declearation. For global it emits the corresponding
 * string (variable name)'s index in ValueArray with OP_DEFINE_GLOBAL along with
 * the OP_CONSTANTs and the corresponding index for it's values. When vm sees
 * it, it maps the variable name and the value in hashtable. So when you do
 * OP_SET_GLOBAL or OP_GET_GLOBAL along with args, this args is nothing by the
 * index of variable name in ValueArray which will be used by vm to look up the
 * corresponding value in the hash table
 *
 *       For local variables it's very very different, almost everything is
 * handled in compile time and all that vm does is pop and push around the
 * values. The 'namespace' maintaining the local variable Compiler compiler is
 * just here in compile time and will not be passed on to vm. Instead, the
 * current->local[] arrays just MIRRORS the arrangement of local variable's
 * values. So what you are passing as args is the slot in vm stack where the
 * value of the variable described will live in the vm.stack NOTE: WHEN YOU PUSH
 * SOMETHING TO VM STACK, YOU ARE PUSHING THE VALUES NOT A BYTECODE!!
 *
 * @param name the variable name that appears in the expression
 * @param canAssign weather it can be assigned a value or not (to avoid
 * something like a*b = c)
 *
 * @return void
 *
 * NOTE: We don't detect undefined error until the runtime cuz we only check for
 * local here, global checks are done in runtime So from compiler's prespective,
 * if it's not local it's "hopefully global"
 *
 */
static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name); // current is the "current" compiler
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg); // to set global expr->setter
  } else {
    emitBytes(getOp, (uint8_t)arg); // to get global expr->getter
  }

  /*
  NOTE: setters vs getters
  setters->an expression that sets a value, assign and stuff
  getter->getting a value, maybe a field's getting a value from class etc
  */
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
  /*
  variable is stored as:
  OP_GET_GLOBAL string
  And we look up the string in hash table
  */
}

static void this_(bool canAssign) {
  if (currentClass == NULL) {
    error("Can't use 'this' outside a class.");
    return;
  }
  variable(false);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type; // only - and ! works here

  // compile the operand
  parsePrecedence(PREC_UNARY);

  // emit the operator instruction
  switch (operatorType) {
  case TOKEN_MINUS:
    emitByte(OP_NEGATE);
    break;
  default:
    return;
  }
}

/**
 * Array of ParseRule where each index is from TokenType enum. This is how you
 initialize multiple array in C. Equivalent for TOKEN_LEFT_PAREN would be:
 *      rules[TOKEN_LEFT_PAREN].prefix = grouping;
        rules[TOKEN_LEFT_PAREN].infix = NULL;
        rules[TOKEN_LEFT_PAREN].precedence = PREC_NONE;
 */
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] =
        {grouping, call, PREC_CALL}, // calling a function is an infix behaviour
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {numbers, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},

};
/**
 * Core of our pratt parser. For each expression, it gets it's precedence,
 */
static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  // to prevent any other expression operation from happening on the left side
  // like a*b = c+d;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}
/**
 * To 'declare' both local and global variable. First declears local variable
 * through declearVariable() On declearVariable it simply adds the local
 * variable's token to compiler For global variable, it uses
 * identifierConstant(), which in turn stores the variable's name as value to
 * chunk's ValueArray and returns the index where it is stored.
 *
 * @param errorMessage the error message to be displayed in case of missing
 * variable name
 *
 * @return uint8_t i.e the index on ValueArray where the global variable's name
 * is stored as OBJ_STRING, returns 0 if it's a local var
 */

static uint8_t parseVariable(const char *errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0)
    return 0; // exit if in local scope

  return identifierConstant(&parser.previous);
}

/**
 * Initializes aka defines the most recently decleared the variable and gives it
 * it's current scope
 *
 * @return void
 */

static void markInitialized() {
  if (current->scopeDepth == 0)
    return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

/**
 * For globals, simply emits OP_DEFINE_GLOBAL, for local calls markInitialized()
 * fnc NOTE: In clox, global variables are "resolved after compile time", i.e we
 * just compile the corresponding global declarations as Bytecode and store the
 * value in chunks. So at run time as the vm goes thru the chunk, it puts the
 * declaraion in hashtable at runtime. For local vars, the mapping of variables
 * to it's value is done at compile time using the Compile struct, making local
 *      variables a little more efficient.
 *
 * @return void
 */
static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global); // OP_DEFINE_GLOBAL's like OP_CONSTANT
                                       // but for declaring global variables
}

/**
 * Returns the entire ParseRule for corresponding operation type
 */
static ParseRule *getRule(TokenType type) { // solely to look up the parser rule
  return &rules[type];
}

/**
 * Starting point for expression parsing.
 * NOTE: expressions have SIDE EFFECTS! When expression chunks are done
 * executing in vm stack they have a +1 effect i.e they leave behind a bytecode
 * in vm stack. That's why it's under expressionStatement, which 'pops' that
 * last bytecode so for an expressionStatement like this: 1+1; the resulting
 * answer, 2 which would have otherwise remained in the vm stack pops. So the
 * above expression outputs nothing. It's different for something like
 * printStatement tho, cuz for: print 1+1; OP_PRINT does the popping as well as
 * printing, which results in 2 ebing printed on screen. Calls parsePrecedence
 * with lowest precedence argument 'PREC_ASSIGNMENT', start of pratt parsing
 * execution NOTE: This interpreter uses recursive descent for parsing
 * declaration and statements, and pratt parsing for expressions
 *
 * @return void
 */

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }
/**
 * For blocks, consumes right and left braces, throws error if closing not found
 *
 * @return void
 */

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, type);
  beginScope(); // we need this so that the functions declared inside this won't
                // be global.
  /*
  This beginScope() doesn’t have a corresponding endScope() call.
   Because we end Compiler completely when we reach the end of the function
  body, there’s no need to close the lingering outermost scope.
  */
  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    // parameters are like local variables, but there's not initialization
    // yet...
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }
      uint8_t constant = parseVariable("Expect parameter name.");
      defineVariable(constant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction *function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalue[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalue[i].index);
  }
}

static void method() {
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t constant = identifierConstant(&parser.previous);

  FunctionType type = TYPE_METHOD;
  function(type);
  emitBytes(OP_METHOD, constant);
}

static void classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token className = parser.previous;
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();
  emitBytes(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  namedVariable(className, false);
  consume(TOKEN_LEFT_BRACE, "Expect { before class body.");

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect } before class body.");
  emitByte(OP_POP);

  currentClass = currentClass->enclosing;
}

static void funDeclaration() {
  uint8_t global = parseVariable("Expect function name.");
  markInitialized(); // we should immeditaly initialize functions after decl for
                     // recursion purposes
  function(TYPE_FUNCTION);
  defineVariable(global);
}

/**
 * Declaring both global and local variables.
 * NOTE: before the variable is defined, it's scope is -1 and in accessible! It
 * is to prevent smthing like: var a = 1;
 *      {
 *          var a = a;
 *      }
 *      Here if we had not implement -1 scoping, when var a = a happens, the
 * inner a tries to assign itself with itself which doesn't have a initialzer
 * yet... but having inner a initiaze as outer a's value makes more sense, So
 * when assignment happens the compiler parses through Local and ignores the
 * inner a declearing seeing it's scoping as -1 and has outer a's value as the
 * assignment target!
 *
 * If a value has been initialized, emits the corresponding value to the chunk
 * through expression(), else emits OP_NIL as initialized value
 *
 * @return void
 */

static void varDeclaration() {
  uint8_t global = parseVariable(
      "Expect variable name"); // global = 0, if the given variable to be
                               // decleard is local. Else global is assigned the
  // corresponding index in ValueArray where the variable name is stored as
  // Obj_String.

  if (match(TOKEN_EQUAL)) {
    expression(); // initializing var, if no initialization, the compiler simply
                  // gives it a nil initialization
  } else {
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  defineVariable(global);
}
/**
 * Parses expression statements (expression + ;), emits OP_POP upon successful
 * parsing or error for semicolen missing
 *
 * @return void
 *
 * NOTE: Expression has a SIDE EFFCT! After it's executed it leaves behind a
 * bytecode as a result, that's why expression statement has a OP_POP at the end
 * to clear out the stack Eg: 1 + 1; Leaves being 2 as the side effect in vm
 * stack but since we are not supposed to display the result, we simply pop the
 * result!
 */
static void
expressionStatement() { // expressionStatemet is expression followed by a ;.
                        // Usually to call a function or evaluate an assignment
                        // for it's side effect
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

static void forStatement() {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  if (match(TOKEN_SEMICOLON)) {

  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;
  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';'after loop condition.");

    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses");
    emitLoops(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }
  statement();
  emitLoops(loopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }
  endScope();
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE))
    statement();
  patchJump(elseJump);
}
/**
 * Emits OP_PRINT bytecode to bytecode chunk or throws error if no SEMICOLEN end
 * of statement
 *
 * @return void
 */
static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }
  if (match(TOKEN_SEMICOLON)) {
    emitReturn(); // returns OP_NIL and OP_RETURN so we implicitly return NIL
  } else {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

static void whileStatement() {
  int loopStart = currentChunk()->count;
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  emitLoops(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}
/**
 * To get the parser out of the panic mode, called at declaration().
 * Passes through keywords util EOF, ; or any other declearitive or
 * statement type tokens
 *
 * @return void
 */

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON)
      return;
    switch (parser.current.type) {
    case TOKEN_CLASS:
    case TOKEN_FUN:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
      return;
    }
    advance();
  }
}

/**
 * First step of the recursive descent. Any declearations made for each token
 * are 'matched' and sent off to be parsed in various functions. If it's not a
 * declaration, it passes thru to statements().
 *
 * Also, if the parser is paniking, calls synchronize to get the tokens in a
 * state where it can parse again
 *
 * @return void
 */
static void declaration() {
  if (match(TOKEN_CLASS)) {
    classDeclaration();
  } else if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode)
    synchronize();
}

/**
 * Second step of the descent, any statements are made here. The statement's
 * keywords are matched, if the token does not preludes a statements, it's let
 * pass through
 *
 * @return void
 */
static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope(); // increases current->scopeDepth by +1
    block();
    endScope(); // decreases current->scopeDepth by -1
  } else {
    expressionStatement();
  }
}

/**
 * Gets the source code and a chunk, turns the source string into bytecode
 * and stores it in the porovided chunk location
 *
 * @param source the source code string
 * @param chunk the compiler chunk location
 * @return bool
 */
ObjFunction *compile(const char *source) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);
  parser.hadError = false;
  parser.panicMode = false;
  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }
  ObjFunction *function = endCompiler();
  return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
  Compiler *compiler = current;
  while (compiler != NULL) {
    markObject((Obj *)compiler->function);
    compiler = compiler->enclosing;
  }
}
