/*
NOTE: Local var needs to be defined before it's used but for globle ones it can be defined anywhere, as long as it's global
declarations are not allowed inside control flow

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
#include <stdio.h>
#include <string.h>
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

static void runtimeError(const char* format, ...){
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm.ip - vm.chunk->code - 1;
    int line = vm.chunk->lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void initVM(){
    resetStack();
    vm.objects = NULL; //no obj allocated for first initialization
    initTable(&vm.globals);
    initTable(&vm.strings);
}

void freeVM(){
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
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

static Value peek(int distance){//looking at whatever's in the vm stack rn
    return vm.stack[vm.stack_count - distance - 1];
}

static bool isFalsey(Value value){
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(){
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length+ 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}


static InterpretResult run(){
#define READ_BYTE() (*vm.ip++)//Note that ip advances as soon as we read the opcode, before we’ve actually started executing the instruction. So, again, ip points to the next byte of code to be used.
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do{ \
        if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){ \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
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
            case OP_CONSTANT:{//values pushed when we see constant
                Value constant = READ_CONSTANT();
                push(constant);
                printValue(constant);
                printf("\n");
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            case OP_GET_GLOBAL:{
                ObjString* name = READ_STRING();
                Value value;
                if(!tableGet(&vm.globals, name, &value)){
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL:{
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();//does not pop until added to hash table just in case the garbage collector triggers in the middle of this process.
                break;
            }
            case OP_SET_GLOBAL:{
                ObjString* name = READ_STRING();
                if(tableSet(&vm.globals, name, peek(0))){//if this is true that means we don't have that specific variable in the table, so we return error.
                    tableDelete(&vm.globals, name); //Also when checking if value exist we created a ghost value. We are deleting the same key for that reason.
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a,b)));
                break;
            }
            case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if(IS_STRING(peek(0)) && IS_STRING(peek(1))){
                    concatenate();
                }
                else if(IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))){
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a+b));
                }
                else{
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE:{
                if(!IS_NUMBER(peek(0))){//check is the value right about in the stack is a number...
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
            case OP_RETURN:{
                return INTERPRET_OK;
            }
        }
    }
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
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