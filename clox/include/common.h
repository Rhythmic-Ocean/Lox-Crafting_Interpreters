#ifndef clox_common_h
#define clox_common_h// to undefine do #undef

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION// this one's a flag u can enable to print all the compiled bytecode of your program for debugging purposes
#define UINT8_COUNT (UINT8_MAX + 1)

#endif//just ending off with the #ifdef stuffs