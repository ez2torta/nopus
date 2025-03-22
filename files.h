#ifndef FILES_H
#define FILES_H

#include "type.h"

typedef struct {
    union {
        s8* data_s8;
        u8* data_u8;
        void* data_void;
    };
    u64 size;
} MemoryFile;

extern const char* FileBasePath;

MemoryFile MemoryFileCreate(const char* path);

void MemoryFileDestroy(MemoryFile* file);

int MemoryFileWrite(MemoryFile* file, const char *path);

#endif // FILES_H