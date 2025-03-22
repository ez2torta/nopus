#include "files.h"

#include <stdlib.h>
#include <stdio.h>

#include "common.h"

const char* FileBasePath = "";

MemoryFile MemoryFileCreate(const char* path) {
    if (path == NULL)
        panic("MemoryFileCreate: path is NULL");

    MemoryFile hndl = {0};

    char fpath[512];
    if (path[0] != '/')
        snprintf(fpath, sizeof(fpath), "%s%s", FileBasePath, path);
    else
        snprintf(fpath, sizeof(fpath), "%s", path);

    FILE* fp = fopen(fpath, "rb");
    if (fp == NULL)
        panic("MemoryFileCreate: fopen failed (path : %s)", fpath);

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        panic("MemoryFileCreate: fseek failed (path : %s)", fpath);
    }

    hndl.size = ftell(fp);
    if (hndl.size == (u64)-1L) {
        fclose(fp);
        panic("MemoryFileCreate: ftell failed (path : %s)", fpath);
    }

    rewind(fp);

    hndl.data_void = malloc(hndl.size);
    if (hndl.data_void == NULL) {
        fclose(fp);
        panic("MemoryFileCreate: malloc failed");
    }

    u64 bytesCopied = fread(hndl.data_void, 1, hndl.size, fp);
    if (bytesCopied < hndl.size && ferror(fp)) {
        fclose(fp);
        free(hndl.data_void);
        panic("MemoryFileCreate: fread failed (path : %s)", fpath);
    }

    fclose(fp);
    return hndl;
}

void MemoryFileDestroy(MemoryFile* file) {
    if (file->data_void)
        free(file->data_void);
    file->data_void = 0;
    file->size = 0;
}

int MemoryFileWrite(MemoryFile* file, const char* path) {
    if (path == NULL) {
        warn("MemoryFileWrite: path is NULL");
        return 1;
    }

    if (file->size > 0 && file->data_void == NULL) {
        warn("MemoryFileWrite: data is NULL");
        return 1;
    }

    char fpath[512];
    if (path[0] != '/')
        snprintf(fpath, sizeof(fpath), "%s%s", FileBasePath, path);
    else
        snprintf(fpath, sizeof(fpath), "%s", path);

    FILE* fp = fopen(fpath, "wb");
    if (fp == NULL) {
        warn("MemoryFileWrite: fopen failed (path : %s)", fpath);
        return 1;
    }

    if (file->size > 0) {
        u64 bytesCopied = fwrite(file->data_void, 1, file->size, fp);
        if (bytesCopied < file->size && ferror(fp)) {
            fclose(fp);
            warn("MemoryFileWrite: fwrite error (path: %s)", fpath);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}
