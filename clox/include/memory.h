#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity)\
   ((capacity) < 8 ? 8 : (capacity) * 2)
// \ is to simply tell the preprocessor that the macro continues on to the next line.
//GROW_CAPACITY's simply makes it to 8 if it's less than 8 and multiply it by 2 if it's any greater
//Know that the number 8 here is chosen arbitarily by the author

#define GROW_ARRAY(type, pointer, oldCount, newCount)\
    (type*)reallocate(pointer, sizeof(type)*(oldCount),\
        sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount)\
    reallocate(pointer, sizeof(type)*(oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void freeObjects();

#endif
