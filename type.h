#ifndef TYPE_H
#define TYPE_H

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef int s32;
typedef short s16;
typedef char s8;

#if defined(_WIN32) || defined (WIN32)
typedef long long s64;
typedef unsigned long long u64;
#else
typedef long s64;
typedef unsigned long u64;
#endif

#endif // TYPE_H
