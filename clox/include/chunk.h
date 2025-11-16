#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef enum{
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_CONSTANT_LONG,
    OP_RETURN,//would late mean return from current function
} OpCode;
//when no value given to any elements in enum, all are assigned int constants

typedef struct{//a dynamic array wrapper
    int count;//no. of elements in the array
    int capacity;//capacity
    int LineCapacity;
    int LineIndex;
    uint8_t* code;
    int* lines;
    int* new_lines;
    ValueArray constants;
} Chunk;//NOTE: count and capcity are the count and capcity of uint8_t code.


/*
When a new element is added and there's enough capacity:
    just increase the count and store at tail end

When new elemented is to be added but not enough capacity:
    allocate new array with more capacity and copy the existing to a new one and delete the old one
    and update the code to point to new array
*/

// Each append is O(1) despite the need to add capacity occasionally. 


void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif