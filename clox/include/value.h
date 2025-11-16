//NOTE: every constant is a value but not every value is a constant
//Value also include outputs that are just stored temporarily
#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum{
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ
}ValueType;

typedef struct{
    ValueType type;
    union{
        bool boolean;
        double number;
        Obj* obj;
    }as;
}Value;

//to check what the lox type is
#define IS_BOOL(value)   ((value).type == VAL_BOOL)
#define IS_NIL(value)    ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value)    ((value).type == VAL_OBJ)

//turns lox type to C types
#define AS_OBJ(value)    ((value).as.obj)
#define AS_BOOL(value)   ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

//turns C types into lox types
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}}) 
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})

typedef struct{//dynamic array to store values (data after output)
    int capacity;
    int count;
    Value* values;
}ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif