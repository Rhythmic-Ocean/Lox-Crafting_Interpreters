#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"


#define TABLE_MAX_LOAD 0.75


void initTable(Table* table){
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table){
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key){
    uint32_t index = key->hash % capacity;
    Entry* tombstone = NULL;
    for(;;){
        Entry* entry = &entries[index];
        if(entry->key == NULL){
            if(IS_NIL(entry->value)){//true empty value, not a tombstone
                return tombstone != NULL ? tombstone : entry;
            }else{//if tombstone continue searching 
                if(tombstone == NULL) tombstone = entry;
            }
        }
        else if(entry->key == key){
            return entry;
        }
        index = (index + 1) % capacity;
    }
}

bool tableGet(Table* table, ObjString* key, Value* value){
    if(table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity,key);
    if(entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

static void adjustCapacity(Table* table, int capacity){
    Entry* entries = ALLOCATE(Entry, capacity);
    for(int i = 0; i < capacity; i++){
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }
    table->count = 0;//to keep track of new table count as we will not be including tombstones here
    for(int i = 0; i < table->capacity; i++){//finding new destination for all key value pairs in the new hash table
        Entry* entry = table->entries;
        if(entry->key == NULL) continue;//tombstones ignored

        Entry* dest = findEntry(entries, capacity,entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool tableSet(Table* table, ObjString* key, Value value){
    if(table->count + 1 > table->capacity * TABLE_MAX_LOAD){
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = (entry->key == NULL);
    if(isNewKey && IS_NIL(entry->value)) {
        table->count++; //only inc count if the new entry goes to a new bucket
        printf("Here it is: %d", table->count);
    }

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, ObjString* key){
    if(table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if(entry->key == NULL) return false;

    entry->key = NULL;
    entry->value = BOOL_VAL(true);//the tombstone for entry
    //also since we add tombstones we don't decrease the count of entries
    return true;
}

void tableAddAll(Table* from, Table* to){//this one is just to copy values from one table to another, independent of adjusting stuff
    for(int i = 0; i < from->capacity; i++){
        Entry* entry = &from->entries[i];
        if(entry->key != NULL){
            tableSet(to,entry->key, entry->value);
        }
    }
}

ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash){
    if(table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;
    for(;;){
        Entry* entry = &table->entries[index];
        if(entry->key == NULL){
            //for empty non tombstone entries
            if(IS_NIL(entry->value)) return NULL;
        }
        else if(entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length) == 0){
            return entry->key;
        }

        index = (index + 1) % table->capacity;
    }
}