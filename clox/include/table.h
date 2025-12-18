#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct{
    ObjString* key;
    Value value;
} Entry;

typedef struct{
    int capacity;
    int count;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
/*Pass the table and key, if it exists value points towards it*/
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table,ObjString* key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);
void tableRemoveWhite(Table* table);
void markTable(Table* table);
#endif