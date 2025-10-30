#include <stdio.h>

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name){
    printf("== %s ==\n", name);

    for(int offset = 0; offset < chunk->count;){
        offset = disassembleInstruction(chunk, offset);
    }
}

static int constantInstruction(const char* name, Chunk* chunk, int offset){
    int constant;
    if(chunk->code[offset] == OP_CONSTANT){
        constant = chunk->code[offset + 1];
        printf("%-16s %4d  '", name, constant);
        printValue(chunk->constants.values[constant]);
        printf("'\n");
        return offset + 2;
    }
    else{
        constant = chunk->code[offset + 1] | (chunk->code[offset + 2] << 8) | (chunk->code[offset + 3] << 16);
        printf("%-16s %4d '", name, constant);
        printValue(chunk->constants.values[constant]);
        printf("'\n");
        return offset + 4;
    }
}//note: static methods are only available in file for useage

static int simpleInstruction(const char* name, int offset){
    printf("%s\n", name);
    return offset + 1;
}

int getAt(Chunk* chunk, int offset){
    for(int i = 0; i < chunk->LineIndex; i++){
        if (offset == chunk->new_lines[i]){
            return chunk->lines[i];
        }
        else if(offset < chunk->new_lines[i]){
            return (i == 0)? chunk->lines[0]: chunk->lines[i - 1];

        }
    }
    return chunk->lines[chunk->LineIndex - 1];//if the loop ends and the valid value still can't be found then 
    //that implies 
}

int disassembleInstruction(Chunk* chunk, int offset){
    printf("%04d  ", offset);

    if(offset >0 &&
            getAt(chunk, offset) == getAt(chunk, offset - 1)){//if a source code comes from the same line as the preceeding line, we do this
        printf(" |");
    }
    else{
        printf("%4d", getAt(chunk, offset));
    }

    uint8_t instruction = chunk->code[offset];
    switch(instruction){
        case OP_CONSTANT_LONG:
            return constantInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}




