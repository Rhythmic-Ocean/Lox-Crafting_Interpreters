//NOTE: every constant is a value but not every value is a constant
//Value also include outputs that are just stored temporarily
#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef double Value;

typedef struct{//dynamic array to store values (data after output)
    int capacity;
    int count;
    Value* values;
}ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif