#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"


static void repl(){
    printf("REPL\n");
    char line[1024];
    for(;;){
        printf(">");
        fflush(stdout);

        if(!fgets(line, sizeof(line), stdin)){//reads from stdin and stores uptop sizeof(line) in a buffer and returns it's pointer to variable line 
            //prevents buffer overflow unlike gets() where u cannot specify the number of characters to take in
            // takes the character until EOF, or \n 
            //automatically appends a \0 at the end of line making it a valid C string
            printf("\n");
            break;
        }
        interpret(line);
    }
}

static char* readFile(const char* path){
    FILE* file = fopen(path, "rb");
    if(file == NULL){
        fprintf(stderr, "Could not open\"%s\".\n ", path);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);//repoints to the starting point

    char* buffer = (char*)malloc(fileSize + 1);
    if(buffer == NULL){
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

    if(bytesRead < fileSize){
        fprintf(stderr, "Could not read file \"%s\".\n", path);
    }
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static void runFile(const char* path){
    char* source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);


    if(result == INTERPRET_COMPILE_ERROR) exit(65);
    if(result == INTERPRET_RUNTIME_ERROR) exit (65);
}


int main(int argc, const char *argv[]){
    initVM();
    if(argc == 1){
        repl();
    }
    else if(argc == 2){
        runFile(argv[1]);
    }
    else{
        fprintf(stderr, "Usage: clox [path]\n");
        exit(64);
    }

    freeVM();
    return 0;
}