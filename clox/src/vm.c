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
/**
 * Intial values are given to the stack elements
 * 
 * @return void 
 */
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
/**
 * Viewing whatever's in the vm stack at (vm.stack_count -distance -1) away, i.e a certain distance away from top
 * 
 * @param distance
 * @return Value from vm.stack
 */ 
static Value peek(int distance){//looking at whatever's in the vm stack rn
    return vm.stack[vm.stack_count - distance - 1];
}

/**
 * if the value is nil, or false
 * 
 * @param value
 * 
 * @return bool
 */
static bool isFalsey(Value value){
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

/**
 * concatenates any two strings that's in the stack consecutively and pushing the final result to stack
 * 
 * @return void
 */
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

/**
 * The core of the Bytecode VM. 
 * 
 * @return InterpretResult
 * 
 * NOTE: each component of the function will be explained inside the function itself since it's so big
 */
static InterpretResult run(){
/**
 * Reads the current vm.ip's value (a location in vm.chunk->code) and increment it by one i.e making the pointer point 
 * to the next bytecode chunk
 */
#define READ_BYTE() (*vm.ip++)
/**
 * Reads the constants from vm.chunk->constant.values array that corresponds to the index in bytecode chunk and returns the Value
 */
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
/**
 * Does the same as READ_CONSTANT(), just turns the corresponding value to OP_STRING object
 */
#define READ_STRING() AS_STRING(READ_CONSTANT())
/**
 * As long as the current and the next character were numbers, all arithmetic except addition's performed in this macro
 */
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
/**
 * When DEBUG_TRACE_EXECUTION flag is turned on, these lines are executed
 * Goes for each bytecode at a time. First it prints whatever's in the vm.stack (if there's anything), then it prints the corresponding
 * bytecode that's being executing in the vm
 * For printing the actual bytecode, two datasets are sent to function disassembleInstruction: vm.chunk and (vm.ip - vm.chunk->code). The first
 * one is just the chunk where the bytecode lives the second one is the offset i.e the position of the said bytecode from the starting position
 */
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
        /**
         * Execution of bytecodes happens here, one at a time
         */
        uint8_t instruction;
        switch(instruction = READ_BYTE()){
            //If the current bytecode is OP_CONSTANT, it looks to next bytecode which is a ValueArray index. Then uses this index
            //to look up the value in ValueArray and push that value in the vm stack
            case OP_CONSTANT:{//values pushed when we see constant
                Value constant = READ_CONSTANT();
                push(constant);
                printValue(constant);
                printf("\n");
                break;
            }
            //For OP_NIL, pushes NIL_VAL, for OP_TRUE pushes Lox_type true and corresponding false for OP_FALSE. For OP_POP, it simpley pops the last
            //value in the stack
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            //here slot is the actual byte (a uint_8 just an integer), it is the index which points where in the vm stack the value required is
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm.stack[slot]);
                break;
            }
            //just like OP_GET_LOCAL, except that chenges the value in the vm stack where the variable's value is to the latest value in the vm stack
            //NOTE: This is an exprStatement, so OP_POP is emitted right after so after assignment the number used for assignment is poped i.e peek(0)
            //is popped right after
            case OP_SET_LOCAL : {
                uint8_t slot = READ_BYTE();
                vm.stack[slot] = peek(0);
                break;
            }
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
/**
 * The core of the interpreter, compiler + vm
 * Initiates chunk calls on compile(), compile fills the &chunk it's given with bytecode it compiled for the corresponding source code
 * If compiler returns false (error), returns with INTERPRET_COMPILE_ERROR and automatically frees the chunk
 * assigns vm.chunk the chunk that the compile() filled
 * vm.ip for now points to the first bytecode on the chunk
 * after complete interpretation, InterpreterResult result will get either EROOR or OK 'result'
 * disassembleChunk() is invoked to show what's in the chunk at last
 * the chunk is freed and the InterpreterResult value is returned
 * 
 * @param source the source code string
 * @return InterpreterResult    INTERPRET_OK, 
                                INTERPRET_COMPILE_ERROR,
                                 INTERPRET_RUNTIME_ERROR,
 * 
 */
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
    disassembleChunk(&chunk, "chunk");

    freeChunk(&chunk);
    return result;
}