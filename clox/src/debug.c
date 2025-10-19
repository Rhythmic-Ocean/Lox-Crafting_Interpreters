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
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
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
    printf("%04d ", offset);

    if(offset >0 &&
            getAt(chunk, offset) == getAt(chunk, offset - 1)){//if a source code comes from the same line as the preceeding line, we do this
        printf(" |");
    }
    else{
        printf("%4d", getAt(chunk, offset));
    }

    uint8_t instruction = chunk->code[offset];
    switch(instruction){
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}




