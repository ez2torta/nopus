#include "list.h"

#include <stdlib.h>

#include <string.h>

#include "common.h"

void ListInit(ListData* list, u32 elementSize, u64 initialCapacity) {
    list->data = malloc(initialCapacity * elementSize);
    if (!list->data)
        panic("ListInit: malloc fail");

    list->elementSize = elementSize;
    list->elementCount = 0;
    list->_capacity = initialCapacity;
}

void* ListGet(ListData* list, u64 index) {
    if (index >= list->elementCount) {
        warn("ListGet: index out of bounds (%u >= %u)\n", index, list->elementCount);
        return NULL;
    }
    return (u8*)list->data + list->elementSize * index;
}


void ListAdd(ListData* list, void* element) {
    if (list->elementCount == list->_capacity) {
        list->_capacity *= 2;

        list->data = realloc(list->data, list->_capacity * list->elementSize);
        if (!list->data)
            panic("ListAdd: malloc fail");
    }

    u8* dst = (u8*)list->data + list->elementSize * list->elementCount++;
    memcpy(dst, element, list->elementSize);
}

void ListRemove(ListData* list, u64 index) {
    if (index >= list->elementCount) {
        warn("ListRemove: index out of bounds (%u >= %u)\n", index, list->elementCount);
        return;
    }

    u8* dst = (u8*)list->data + list->elementSize * index;
    u8* src = dst + list->elementSize;
    u64 bytesToMove = (list->elementCount - index - 1) * list->elementSize;
    
    memmove(dst, src, bytesToMove);
    list->elementCount--;

    if (list->elementCount < list->_capacity / 4) {
        list->_capacity /= 2;
        list->data = realloc(list->data, list->_capacity * list->elementSize);
    }
}

void ListAddRange(ListData* list, void* elements, u64 count) {
    if (list->elementCount + count > list->_capacity) {
        while (list->elementCount + count > list->_capacity)
            list->_capacity *= 2;

        void* newData = realloc(list->data, list->_capacity * list->elementSize);
        if (!newData)
            panic("ListAddRange: realloc fail");

        list->data = newData;
    }
    
    u8* dst = (u8*)list->data + list->elementSize * list->elementCount;
    memcpy(dst, elements, count * list->elementSize);
    list->elementCount += count;
}

void ListDestroy(ListData* list) {
    free(list->data);
    list->data = NULL;
    list->elementCount = 0;
    list->_capacity = 0;
}
