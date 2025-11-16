#include <stdio.h>
#include <string.h>


#include "memory.h"
#include "object.h"
#include "value.h"
#include "table.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type){
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash){
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    tableSet(&vm.strings, string, NIL_VAL);//the table's a hash set, only keys not values
    printf("%d", string->length);
    return string;
}

static uint32_t hashString(const char* key, int length){ //hashing given string
    uint32_t hash = 2166136261u;
    for(int i = 0; i < length; i++){
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* takeString(char* chars, int length){//for new string after concatination
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if(interned != NULL){//if the concatinated string's already interned, free the current one and return the reference to interned one
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length){
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars,length, hash);//checking if the string's already interned, if yes return that reference, else fall through 
    if(interned != NULL) return interned;
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

void printObject(Value value){
    Obj* obj = value.as.obj;
    switch(obj->type){
        case OBJ_STRING: {
            ObjString* string = (ObjString*)obj;
            printf("%s", string->chars);
            break;
        }
    }


}
    