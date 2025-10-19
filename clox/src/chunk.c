//NOTE: CODE is stored in chunks like 1+2; might be a code so it's stored
//but it's output, 3 is DATA and not stored in chunk

#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk* chunk){//this chunk has already been malloced smwhere else
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->LineCapacity = 0;
    chunk->LineIndex = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->new_lines = NULL;
    initValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line){
    if(chunk->capacity < chunk->count + 1){
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    }

    if(chunk->LineCapacity < chunk->LineIndex + 1){
        int oldCapacity = chunk->LineCapacity;
        chunk->LineCapacity = GROW_CAPACITY(oldCapacity);
        chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->LineCapacity);
        chunk->new_lines= GROW_ARRAY(int, chunk->new_lines, oldCapacity, chunk->LineCapacity);
        
    }
    chunk->code[chunk->count] = byte;
    if(chunk->LineIndex == 0 || chunk->lines[chunk->LineIndex - 1] != line){
        chunk->lines[chunk->LineIndex] = line;
        chunk->new_lines[chunk->LineIndex] = chunk->count;
        chunk->LineIndex++;
    }
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value){
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count -1;
}

void freeChunk(Chunk* chunk){
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->LineCapacity);
    FREE_ARRAY(int, chunk->new_lines, chunk->LineCapacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}