#include <stdlib.h>

#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize){
    if(newSize == 0){ //newSize = 0 implies we want to free the pointer
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);//else, the given pointer will be resized to a new size
    if (result == NULL) exit(1);//if not enough memory
    return result;
}//this is for dynamic memory management so we can allocate memory at will.