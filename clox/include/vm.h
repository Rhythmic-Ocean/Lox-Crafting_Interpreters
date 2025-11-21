#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"

#define INIT_STACK 256

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
typedef struct{
    Chunk* chunk;
    uint8_t* ip;//pointer for where we are currently at in the bytecode instructions
    //ip = instruction pointer
    //JVM calls it PC
    int stack_size;
    int stack_count;
    Value* stack;
    Table globals;
    Table strings;
    Obj* objects;//head of the obj list for garbage collection
}VM;

/**
 * End output of the bytecode vm
 */
typedef enum{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push (Value value);
Value pop();

#endif
