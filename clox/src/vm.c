#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "memory.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

VM vm;

struct timespec start, end;

static void resetStack(){
    vm.stack = malloc(INIT_STACK * sizeof(Value));
    vm.stack_size = INIT_STACK;
    vm.stack_count = 0;
}

void initVM(){
    resetStack();
}

void freeVM(){
    free(vm.stack);
    vm.stack = NULL;
    vm.stack_size = 0;
    vm.stack_count = 0;
}

void resize_vm(){
    int oldCapacity = vm.stack_size;
    vm.stack_size = GROW_CAPACITY(oldCapacity);
    vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, vm.stack_size);

}

void push (Value value){
    if(vm.stack_size <= vm.stack_count){
        resize_vm();
    }
    vm.stack[vm.stack_count] = value;
    vm.stack_count++;
}
Value pop(){
    vm.stack_count--;
    return vm.stack[vm.stack_count];
}
Value negate(){
    vm.stack[vm.stack_count - 1] *=-1;
}

static InterpretResult run(){
#define READ_BYTE() (*vm.ip++)//Note that ip advances as soon as we read the opcode, before we’ve actually started executing the instruction. So, again, ip points to the next byte of code to be used.
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op) \
    do{ \
        double b = pop(); \
        double a = pop(); \
        push(a op b); \
    }while (false)
    
    for(;;){
#ifdef DEBUG_TRACE_EXECUTION
        printf("    ");
        for(Value* slot = vm.stack; slot < vm.stack+vm.stack_count; slot++){
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
        uint8_t instruction;
        switch(instruction = READ_BYTE()){
            case OP_ADD: BINARY_OP(+); break;//values poped when we see operaters
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE: BINARY_OP(/); break;
            case OP_NEGATE: push(-pop()); break;
            case OP_CONSTANT:{//values pushed when we see constant
                Value constant = READ_CONSTANT();
                push(constant);
                printValue(constant);
                printf("\n");
                break;
            }
            case OP_RETURN:{
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(const char* source){
    Chunk chunk;
    initChunk(&chunk);

    bool compiled = compile(source, &chunk);

    if(!compiled){//compile() returns false when some parsing error comes up
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();
    disassembleChunk(&chunk, "final chunk");
    freeChunk(&chunk);
    return result;
}