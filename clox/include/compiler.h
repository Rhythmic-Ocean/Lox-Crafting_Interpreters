#ifndef clox_compiler_h
#define clox_compiler_h

#include "vm.h"
extern int a;
bool compile(const char* source, Chunk* chunk);

#endif