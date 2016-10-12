// DO NOT CHANGE!!!

#ifndef MEM_H
#define MEM_H

typedef enum {ERR_BAD_ARGS = 1, ERR_OUT_OF_SPACE = 2, ERR_MEM_UNINITIALIZED = 3, ERR_INVALID_FREE = 4} Mem_Error;
Mem_Error m_err;

int Mem_Init(int size);
void * Mem_Alloc(int size);
int Mem_Free(void *ptr);
void Mem_Dump();

#endif
