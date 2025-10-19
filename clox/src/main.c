#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, const char *argv[]){
    Chunk chunk;//now this chunk is stored in stack i.e. it's lifetime is as long as the function returns
    initChunk(&chunk);
    int constant = addConstant(&chunk, 1.2);
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 124);
    writeChunk(&chunk, OP_RETURN, 125);

    disassembleChunk(&chunk, "test chunk");
    freeChunk(&chunk);
    return 0;
}