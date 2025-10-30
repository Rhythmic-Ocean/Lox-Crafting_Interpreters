#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define INIT_STACK 256
typedef struct{
    Chunk* chunk;
    uint8_t* ip;//pointer for where we are currently at in the bytecode instructions
    //ip = instruction pointer
    //JVM calls it PC
    int stack_size;
    int stack_count;
    Value* stack;
}VM;

typedef enum{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push (Value value);
Value pop();

#endif
