#ifndef LIST_H
#define LIST_H

#include "type.h"

typedef struct {
    void* data;

    u64 elementCount;
    u32 elementSize;

    u64 _capacity;
} ListData;

void ListInit(ListData* list, u32 elementSize, u64 initialCapacity);

void* ListGet(ListData* list, u64 index);

void ListAdd(ListData* list, void* element);
void ListRemove(ListData* list, u64 index);

void ListAddRange(ListData* list, void* elements, u64 count);

void ListDestroy(ListData* list);

#endif // LIST_H
