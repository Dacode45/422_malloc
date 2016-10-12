#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define ITERATIONS 10000000
#define MAXSIZE 504


typedef struct __node_t {
    int size;
    void * addr;
} node_t;

#define NUMPAGES 16
#define MAXALLOCS 256
#define MEMSIZE ((getpagesize() * NUMPAGES) - (MAXALLOCS * sizeof(node_t)))

int main(int argc, char * argv[]){
   time_t t;
   int bytes_allocated = 0;
   srand((unsigned) time(&t));
   printf("running\n");
   int err = Mem_Init(getpagesize() * NUMPAGES);
   if (err == 0){
	printf("heap initialized\n");
   }
   else {
	printf("error: %d\n", m_err);
        return -2;
   }
   node_t * allocs = (node_t *)Mem_Alloc(MAXALLOCS * sizeof(node_t));
   bytes_allocated = MAXALLOCS * sizeof(node_t) + 8;
   int i;
   for(i = 0; i < MAXALLOCS; i++){
      allocs[i].size = 0;
   }
   clock_t start = clock();
   for(i = 0; i < ITERATIONS; i++){
       int index = rand() % MAXALLOCS;
       if(allocs[index].size == 0){
           if(bytes_allocated + MAXSIZE < MEMSIZE){
              int size = (rand() % MAXSIZE);
              int off = size % 8;
              size += (8 - off);
 	      void * temp = Mem_Alloc(size);
              if (temp == NULL){
	         printf("failed alloc of size: %d\n",size);
                 Mem_Dump();
                 return -1;
              }
              allocs[index].addr = temp;
              allocs[index].size = size + 8;
              bytes_allocated += size + 8;
           }
       }
       else{
	   bytes_allocated -= allocs[index].size;
           int c = Mem_Free(allocs[index].addr);
           allocs[index].size = 0;
           if(c == -1){
               printf("invalid free\n");
               return -2;
           }
       }
       //printf("iteration: %d     bytes allocated = %d\n",i,bytes_allocated);
   }
   clock_t diff = clock() - start;
   Mem_Dump();
   printf("done\n");
   for(i = 0; i < MAXALLOCS; i++){
       if(allocs[i].size != 0){
           Mem_Free(allocs[i].addr);
           bytes_allocated -= allocs[i].size;
       }
   }
   Mem_Free(allocs);
   Mem_Dump();
   int msec = diff * 1000 / CLOCKS_PER_SEC;
   printf("Elapsed Time: %d seconds %d ms\n",msec/1000, msec%1000);
   return 0;
}
