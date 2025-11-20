#include <stdlib.h>

#include "memory.h"
#include "vm.h"

/**
 * Given a pointer, it's existing size and new desired size, this function returns the pointer
 * of type void with desired size allocated. If desired size = 0, NULL is returned with given pointer freed
 * 
 * @param pointer
 * @param oldSize
 * @param newSize
 * 
 * @return void*
 */
void* reallocate(void* pointer, size_t oldSize, size_t newSize){
    if(newSize == 0){ //newSize = 0 implies we want to free the pointer
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);//else, the given pointer will be resized to a new size
    if (result == NULL) exit(1);//if not enough memory
    return result;
}//this is for dynamic memory management so we can allocate memory at will.

static void freeObject(Obj* object){
    switch (object->type){
        case OBJ_STRING:{
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length+1);
            FREE(ObjString, object);
            break;
        }
    }
}

void freeObjects(){
    Obj* object = vm.objects;
    while(object != NULL){
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}