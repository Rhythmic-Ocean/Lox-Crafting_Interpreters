/*
NOTE: Local var needs to be defined before it's used but for globle ones it can
be defined anywhere, as long as it's global declarations are not allowed inside
control flow

statement      → exprStmt
               | forStmt
               | ifStmt
               | printStmt
               | returnStmt
               | whileStmt
               | block ;

Then we use a separate rule for the top level of a script and inside a block.

declaration    → classDecl
               | funDecl
               | varDecl
               | statement ;

Total stack effect of statements = 0
For expression that's +1

*/

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

struct timespec start, end;
/**
 * Intial values are given to the stack elements
 *
 * @return void
 */
static void resetStack() {
  vm.stack = malloc(INIT_STACK * sizeof(Value));
  vm.stack_size = INIT_STACK;
  vm.stack_count = 0;
  vm.frameCount = 0;
  vm.openUpvalues = 0;
}

static void runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame *frame = &vm.frames[i];
    ObjFunction *function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }
  resetStack();
}

static void defineNative(const char *name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

void initVM() {
  resetStack();
  vm.objects = NULL; // no obj allocated for first initialization
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;
  vm.grayCapacity = 0;
  vm.grayCount = 0;
  vm.grayStack = NULL;

  initTable(&vm.globals);
  initTable(&vm.strings);
  vm.initString = NULL;

  vm.initString = copyString("init", 4);

  defineNative("clock", clockNative);
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
  free(vm.stack);
  vm.stack = NULL;
  vm.stack_size = 0;
  vm.stack_count = 0;
}

void resize_vm() {
  int oldCapacity = vm.stack_size;
  vm.stack_size = GROW_CAPACITY(oldCapacity);
  vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, vm.stack_size);
}

void push(Value value) {
  if (vm.stack_size <= vm.stack_count) {
    resize_vm();
  }
  vm.stack[vm.stack_count] = value;
  vm.stack_count++;
}

Value pop() {
  vm.stack_count--;
  return vm.stack[vm.stack_count];
}
/**
 * Viewing whatever's in the vm stack at (vm.stack_count -distance -1) away, i.e
 * a certain distance away from top
 *
 * @param distance
 * @return Value from vm.stack
 */
static Value peek(int distance) { // looking at whatever's in the vm stack rn
  return vm.stack[vm.stack_count - distance - 1];
}

static bool call(ObjClosure *closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.", closure->function->arity,
                 argCount);
    return false;
  }
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack Overflow.");
    return false;
  }
  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stack_count - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
      vm.stack[vm.stack_count - argCount - 1] = bound->receiver;
      return call(bound->method, argCount);
    }
    case OBJ_CLASS: {
      ObjClass *klass = AS_CLASS(callee);
      vm.stack[vm.stack_count - argCount - 1] = OBJ_VAL(newInstance(klass));

      Value initializer;
      if (tableGet(&klass->methods, vm.initString, &initializer)) {
        return call(AS_CLOSURE(initializer), argCount);
      } else if (argCount != 0) {
        runtimeError("Expected 0 arguments, got %d", argCount);
        return false;
      }
      return true;
    }
    case OBJ_CLOSURE: {
      return call(AS_CLOSURE(callee), argCount);
    }
    case OBJ_NATIVE: {
      NativeFn native = AS_NATIVE(callee);
      Value result = native(argCount, vm.stack + vm.stack_count - argCount);
      vm.stack_count -= argCount + 1;
      push(result);
      return true;
    }
    default:
      break; // non callable object type
    }
  }

  runtimeError("Can only call functions and classes.");
  return false;
}

static bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString *name, int argCount) {
  Value receiver = peek(argCount);
  ObjInstance *instance = AS_INSTANCE(receiver);
  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    vm.stack[vm.stack_count - argCount - 1] = value;
    return callValue(value, argCount);
  }
  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }
  return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(ObjClass *klass, ObjString *name) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));
  pop();
  push(OBJ_VAL(bound));
  return true;
}
// now if two closures captures the same local (even siblings ones), they will
// have the same upvalue
static ObjUpvalue *captureUpvalue(Value *local) {
  ObjUpvalue *preUpvalue = NULL;
  ObjUpvalue *upvalue =
      vm.openUpvalues; // if we create a new Upvalue here this will be the next
                       // element and preUpvalue will be the previous one
  while (upvalue != NULL && upvalue->location > local) {
    preUpvalue = upvalue;
    upvalue = upvalue->next;
  }
  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  // if the required upvalue's not already in the linked list, we make a new one
  ObjUpvalue *createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (preUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    preUpvalue->next = createdUpvalue;
  }
  return createdUpvalue;
}

static void closeUpvalues(Value *last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static void defineMethod(ObjString *name) {
  Value method = peek(0);
  ObjClass *klass = AS_CLASS(peek(1));
  tableSet(&klass->methods, name, method);
  pop();
}
/**
 * if the value is nil, or false
 *
 * @param value
 *
 * @return bool
 */
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

/**
 * concatenates any two strings that's in the stack consecutively and pushing
 * the final result to stack
 *
 * @return void
 */
static void concatenate() {
  ObjString *b = AS_STRING(peek(0)); // we peek here an pop later just in cases
                                     // the GC triggers mid way to concatenation
  // and sweeps the unmarked strings that's not tracable (not in the stack or
  // anywhere, since they were popped) so we peek instead of pop rn, and pop it
  // later when we truely don't need them!!
  ObjString *a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char *chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = takeString(chars, length);
  pop();
  pop();
  push(OBJ_VAL(result));
}

/**
 * The core of the Bytecode VM.
 *
 * @return InterpretResult
 *
 * NOTE: each component of the function will be explained inside the function
 * itself since it's so big
 */
static InterpretResult run() {
  CallFrame *frame = &vm.frames[vm.frameCount - 1];
/**
 * Reads the current vm.ip's value (a location in vm.chunk->code) and increment
 * it by one i.e making the pointer point to the next bytecode chunk
 */
#define READ_BYTE() (*frame->ip++)

  /**
   * Yanks the next 2 bytes from the chunk and builds a 16 bit integer out of it
   */
#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8 | frame->ip[-1])))
/**
 * Reads the constants from vm.chunk->constant.values array that corresponds to
 * the index in bytecode chunk and returns the Value
 */
#define READ_CONSTANT()                                                        \
  (frame->closure->function->chunk.constants.values[READ_BYTE()])
  /**
   * Does the same as READ_CONSTANT(), just turns the corresponding value to
   * OP_STRING object
   */

#define READ_STRING() AS_STRING(READ_CONSTANT())
/**
 * As long as the current and the next character were numbers, all arithmetic
 * except addition's performed in this macro
 */
#define BINARY_OP(valueType, op)                                               \
  do {                                                                         \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                          \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    double b = AS_NUMBER(pop());                                               \
    double a = AS_NUMBER(pop());                                               \
    push(valueType(a op b));                                                   \
  } while (false)

  for (;;) {
/**
 * When DEBUG_TRACE_EXECUTION flag is turned on, these lines are executed
 * Goes for each bytecode at a time. First it prints whatever's in the vm.stack
 * (if there's anything), then it prints the corresponding bytecode that's being
 * executing in the vm For printing the actual bytecode, two datasets are sent
 * to function disassembleInstruction: vm.chunk and (vm.ip - vm.chunk->code).
 * The first one is just the chunk where the bytecode lives the second one is
 * the offset i.e the position of the said bytecode from the starting position
 */
#ifdef DEBUG_TRACE_EXECUTION
    printf("    ");
    for (Value *slot = vm.stack; slot < vm.stack + vm.stack_count; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif
    /**
     * Execution of bytecodes happens here, one at a time
     */
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
    // If the current bytecode is OP_CONSTANT, it looks to next bytecode which
    // is a ValueArray index. Then uses this index to look up the value in
    // ValueArray and push that value in the vm stack
    case OP_CONSTANT: { // values pushed when we see constant
      Value constant = READ_CONSTANT();
      push(constant);
      printf("\n");
      break;
    }
    // For OP_NIL, pushes NIL_VAL, for OP_TRUE pushes Lox_type true and
    // corresponding false for OP_FALSE. For OP_POP, it simpley pops the last
    // value in the stack
    case OP_NIL:
      push(NIL_VAL);
      break;
    case OP_TRUE:
      push(BOOL_VAL(true));
      break;
    case OP_FALSE:
      push(BOOL_VAL(false));
      break;
    case OP_POP:
      pop();
      break;
    // here slot is the actual byte (a uint_8 just an integer), it is the index
    // which points where in the vm stack the value required is
    case OP_GET_LOCAL: {
      uint8_t slot = READ_BYTE();
      push(vm.stack[frame->slots + slot]);
      break;
    }
    // just like OP_GET_LOCAL, except that chenges the value in the vm stack
    // where the variable's value is to the latest value in the vm stack NOTE:
    // This is an exprStatement, so OP_POP is emitted right after so after
    // assignment the number used for assignment is poped i.e peek(0) is popped
    // right after
    case OP_SET_LOCAL: {
      uint8_t slot = READ_BYTE();
      vm.stack[frame->slots + slot] = peek(0);
      break;
    }
    case OP_GET_GLOBAL: {
      ObjString *name = READ_STRING();
      Value value;
      if (!tableGet(&vm.globals, name, &value)) {
        runtimeError("Undefined variable '%s'", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      push(value);
      break;
    }
    case OP_DEFINE_GLOBAL: {
      ObjString *name = READ_STRING();
      tableSet(&vm.globals, name, peek(0));
      pop(); // does not pop until added to hash table just in case the garbage
             // collector triggers in the middle of this process.
      break;
    }
    case OP_SET_GLOBAL: {
      ObjString *name = READ_STRING();
      if (tableSet(
              &vm.globals, name,
              peek(0))) { // if this is true that means we don't have that
                          // specific variable in the table, so we return error.
        tableDelete(
            &vm.globals,
            name); // Also when checking if value exist we created a ghost
                   // value. We are deleting the same key for that reason.
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_GET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      push(*frame->closure->upvalues[slot]->location);
      break;
    }
    case OP_SET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      printValue(peek(0));
      *frame->closure->upvalues[slot]->location = peek(0);
      break;
    }
    case OP_GET_PROPERTY: {
      if (!IS_INSTANCE(peek(0))) {
        runtimeError("Only instances have property.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjInstance *instance = AS_INSTANCE(peek(0));
      ObjString *name = READ_STRING();

      Value value;
      if (tableGet(&instance->fields, name, &value)) {
        pop();
        push(value);
        break;
      }
      if (!bindMethod(instance->klass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SET_PROPERTY: {
      if (!IS_INSTANCE(peek(1))) {
        runtimeError("Only instances have property.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjInstance *instance = AS_INSTANCE(peek(1));
      tableSet(&instance->fields, READ_STRING(), peek(0));
      Value value = pop();
      pop();
      push(value);
      break;
    }
    case OP_EQUAL: {
      Value b = pop();
      Value a = pop();
      push(BOOL_VAL(valuesEqual(a, b)));
      break;
    }
    case OP_GREATER:
      BINARY_OP(BOOL_VAL, >);
      break;
    case OP_LESS:
      BINARY_OP(BOOL_VAL, <);
      break;
    case OP_ADD: {
      if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
        concatenate();
      } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
        double b = AS_NUMBER(pop());
        double a = AS_NUMBER(pop());
        push(NUMBER_VAL(a + b));
      } else {
        runtimeError("Operands must be two numbers or two strings.");
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SUBTRACT:
      BINARY_OP(NUMBER_VAL, -);
      break;
    case OP_MULTIPLY:
      BINARY_OP(NUMBER_VAL, *);
      break;
    case OP_DIVIDE:
      BINARY_OP(NUMBER_VAL, /);
      break;
    case OP_NOT:
      push(BOOL_VAL(isFalsey(pop())));
      break;
    case OP_NEGATE: {
      if (!IS_NUMBER(peek(0))) { // check is the value right about in the stack
                                 // is a number...
        runtimeError("Operant must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }

      push(NUMBER_VAL(-AS_NUMBER(pop())));
      break;
    }
    case OP_PRINT: {
      printValue(pop());
      printf("\n");
      break;
    }
    // Note that if OP_JUMP_IF_FALSE increaes the vm.ip's count, it's directly
    // set to be inside the else condition so it will skip this bytecode
    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      break;
    }
    // Here this jump instruction does not automaticaly pops condition value, we
    // still need to operate on that
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();
      if (isFalsey(peek(0)))
        frame->ip += offset;
      break;
    }
    case OP_LOOP: {
      uint16_t offset = READ_SHORT();
      frame->ip -= offset;
      break;
    }
    case OP_CALL: {
      int argCount = READ_BYTE();
      if (!callValue(peek(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_INVOKE: {
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();
      if (!invoke(method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLOSURE: {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
      ObjClosure *closure = newClosure(function);
      push(OBJ_VAL(closure));
      for (int i = 0; i < closure->upvalueCount; i++) {
        uint8_t isLocal = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (isLocal) {
          closure->upvalues[i] =
              captureUpvalue((Value *)(vm.stack + frame->slots + index));
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OP_CLOSE_UPVALUE: { // it's emitted right before end of the function
                             // call where the the local equivalent of our
                             // upvalue lives
      closeUpvalues(vm.stack + vm.stack_count - 1);
      pop();
      break;
    }
    case OP_RETURN: {
      Value result = pop();
      closeUpvalues((Value *)(vm.stack + frame->slots));
      vm.frameCount--;
      if (vm.frameCount == 0) {
        pop();
        return INTERPRET_OK;
      }

      vm.stack_count = frame->slots;
      push(result);
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLASS: {
      push(OBJ_VAL(newClass(READ_STRING())));
      break;
    }
    case OP_METHOD: {
      defineMethod(READ_STRING());
      break;
    }
    }
  }
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}
/**
 * The core of the interpreter, compiler + vm
 * Initiates chunk calls on compile(), compile fills the &chunk it's given
 with bytecode it compiled for the corresponding source code
 * If compiler returns false (error), returns with INTERPRET_COMPILE_ERROR and
 automatically frees the chunk
 * assigns vm.chunk the chunk that the compile() filled
 * vm.ip for now points to the first bytecode on the chunk
 * after complete interpretation, InterpreterResult result will get either
 EROOR or OK 'result'
 * disassembleChunk() is invoked to show what's in the chunk at last
 * the chunk is freed and the InterpreterResult value is returned
 *
 * @param source the source code string
 * @return InterpreterResult    INTERPRET_OK,
                                INTERPRET_COMPILE_ERROR,
                                 INTERPRET_RUNTIME_ERROR,
 *
 */
/**
 * The code looks a little silly because we still push the original
 * ObjFunction onto the stack. Then we pop it after creating the closure, only
 * to then push the closure. Why put the ObjFunction on there at all? As
 * usual, when you see weird stack stuff going on, it’s to keep the
 * forthcoming garbage collector aware of some heap-allocated objects.
 */
InterpretResult interpret(const char *source) {
  ObjFunction *function = compile(source);
  if (function == NULL)
    return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  ObjClosure *closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);
  return run();
}
