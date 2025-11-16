#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

#define IS_STRING(value)    isObjType(value, OBJ_STRING)

#define AS_STRING(value)     ((ObjString*)AS_OBJ(value))//returns pointer to ObjString type
#define AS_CSTRING(value)    (((ObjString*)AS_OBJ(value))->chars)//returns the string array itself

typedef enum{
    OBJ_STRING
}ObjType;

struct Obj{
    ObjType type;
    struct Obj* next;
};

struct ObjString{
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};//now Obj is like super sturct
/* you can do 
ObjString* s = ...;
Obj* base = (Obj*)s;
*/

ObjString* takeString(char* chars, int length);

ObjString* copyString(const char* chars,int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type){
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}//here we did not put the fnc body inside the macro cuz the body uses "value" twice
//So if the first operation to the parameter (say if value was a function is pop()) has some sideeffect, it will be unintentionally executed twice causing problems

#endif