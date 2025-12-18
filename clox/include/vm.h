#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define INIT_STACK 256
#define FRAMES_MAX 64
#define STACK_MAX (FRAME_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure *closure;
  uint8_t *ip;
  int slots;
} CallFrame;

/**
 * This is where the entire data from source code gets evaluated
 * Chunk stores the bytecodes from compiler
 * ip points to any one memory point in chunk->code, for bytecode execution
 * stack_size -> capacite of the vm stack
 * stack_count -> current count of bytecode in the stack
 * globals -> hashtable with all the globals
 * strings -> hashtable with all the OBJ_STRINGs
 * object -> head of the obj list for GC
 */
typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;
  int stack_size;
  int stack_count;
  Value *stack;
  Table globals;
  Table strings;
  ObjString *initString;
  ObjUpvalue
      *openUpvalues; // head of the linked list storing all the open upvalues
  size_t bytesAllocated; // number of bytes the vm manages
  size_t nextGC;         // the threashold on bytes where the GC starts up
  Obj *objects; // head of the obj list for garbage collection, has every single
                // object ever created
  int grayCount;
  int grayCapacity;
  Obj **grayStack;
} VM;

/**
 * End output of the bytecode vm
 */
typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char *source);
void push(Value value);
Value pop();

#endif
