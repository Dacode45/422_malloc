#include <unistd.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

#include "mem.h"

/**********************************
* HEAP DEFINITION
**********************************/
void *heap_start = NULL;
void *heap_end = NULL;
int heap_size = 0;

/**********************************
* Alignment Definitino
**********************************/
// double world (8) alignment. single word (4)
#define ALIGNMENT 8

// Rounds up to the nearest multiple of ALIGNMENT
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// http://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
const int tab32[32] = {
     0,  9,  1, 10, 13, 21,  2, 29,
    11, 14, 16, 18, 22, 25,  3, 30,
     8, 12, 20, 28, 15, 17, 24,  7,
    19, 27, 23,  6, 26,  5,  4, 31 };
static inline int log2_32(uint32_t value) {
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return tab32[(uint32_t)(value*0x07C4ACDD) >> 27];
}


/**
Block macros
* void* previous
* size_t size | free bit
* (allocated data) | (void * next_of_same_size; void* prev_of_same_size)
*
* ie seglists. Chunk to powers of 2 seems reasonable.
*/

typedef void * block_node;
#define MAX(a,b) ((a > b) ? a : b)
#define MIN(a, b) ((a < b) ? a : b)

#define BLOCK_HEADER_SIZE ALIGN((sizeof(block_node) + sizeof(size_t)))
#define MIN_DATA_SIZE ALIGN(2*sizeof(block_node))

#define GET_BLOCK_SIZE(size) (ALIGN(MAX(size, MIN_DATA_SIZE) + BLOCK_HEADER_SIZE))
#define GET_HEADER(dataptr) ((block_node)((char*)dataptr - BLOCK_HEADER_SIZE))
#define GET_DATA(blockptr) ((void*)((char*)blockptr + BLOCK_HEADER_SIZE))

//cast block into a pointer to a block node then dereference it
#define GET_PREVIOUS_BLOCK(currentBlock) *(block_node*)currentBlock
#define SET_PREVIOUS_BLOCK(blockPointer, previous) (GET_PREVIOUS_BLOCK(blockPointer) = (block_node)previous)
#define GET_NEXT_BLOCK(currentBlock) (block_node)((char*)currentBlock + BLOCK_HEADER_SIZE+GET_SIZE(currentBlock))

//when a block is free, the part used for is data becomes a BlockFooter
#define GET_NEXT_FREE_BLOCK(currentBlock)(*(block_node*)((char*)currentBlock + BLOCK_HEADER_SIZE))
#define SET_NEXT_FREE_BLOCK(currentBlock, next) (GET_NEXT_FREE_BLOCK(currentBlock) = (block_node)next)
#define GET_PREVIOUS_FREE_BLOCK(currentBlock) (*(block_node*)((char*)currentBlock + BLOCK_HEADER_SIZE + sizeof(block_node)))
#define SET_PREVIOUS_FREE_BLOCK(currentBlock, prev) (GET_PREVIOUS_FREE_BLOCK(currentBlock) = (block_node)prev)

#define FREE_MASK (1UL<<(sizeof(size_t)*8 - 1))
#define GET_MASKED_SIZE_POINTER(blockPointer) ((size_t*)((char*)blockPointer + sizeof(block_node)))
#define GET_MASKED_SIZE(blockPointer) (*GET_MASKED_SIZE_POINTER(blockPointer))
#define IS_FREE(blockPointer) (GET_MASKED_SIZE(blockPointer) & (FREE_MASK))
#define SET_FREE(blockPointer, free) (GET_MASKED_SIZE(blockPointer) = free << (sizeof(size_t)*8 -1) | (GET_MASKED_SIZE(blockPointer) << 1 >> 1))
#define GET_SIZE(blockPointer) (GET_MASKED_SIZE(blockPointer) & ~FREE_MASK)
#define SET_SIZE(blockPointer, size) ((GET_MASKED_SIZE(blockPointer)) = (size | IS_FREE(blockPointer)))

/**
  * Bookkeeping
*/
block_node BASE = NULL;
block_node END = NULL;

// Our free list store 2^i up to 2^(i+1),
// so anything in bin i is going to be size s:
// 2^i <= s < 2^(i+1)
#define FREE_LIST_COUNT 20
block_node FREE_LIST[FREE_LIST_COUNT];
#define GET_FREE_LIST_NUMBER(size) (MIN(log2_32(size), FREE_LIST_COUNT-1))

/*
 * returns non-zero if macros function properly
 */
int macro_checker()
{
  /**
   * BLOCK_HEADER_SIZE
   */
  // should be 8 bytes
  // b/c sizeof(void*) = 4, sizeof(size_t) = 4
  // 4 + 4 = 8
  assert(sizeof(block_node) == ALIGNMENT);
  assert(sizeof(void*) == ALIGNMENT);
  assert(sizeof(size_t) == ALIGNMENT);
  assert(BLOCK_HEADER_SIZE == 2*ALIGNMENT);

  /**
   * Header & Data getters
   */
  // should be inverses
   const size_t testNodeSize = 8;
  block_node testNode = malloc(GET_BLOCK_SIZE(testNodeSize));
  assert(GET_HEADER(GET_DATA(testNode)) == testNode);
  // should move the right amount (ie BLOCK_HEADER_SIZE)
  // because: o o o o|o o o o w/ header_size = 4
  //         ^
  // should:  o o o o|o o o o
  //                 ^
  assert(GET_DATA(testNode) == testNode + BLOCK_HEADER_SIZE);

  /**
   * Size and Free
   */
  // should accurately read a free set
  GET_MASKED_SIZE(testNode) = 1UL << (sizeof(size_t)*8 - 1);
  assert(IS_FREE(testNode));
  assert(GET_SIZE(testNode) == 0);
  // should accurately read free set even if size is still set
  GET_MASKED_SIZE(testNode) = ~0;
  assert(IS_FREE(testNode));
  // should accurately read a non-free set
  GET_MASKED_SIZE(testNode) = GET_MASKED_SIZE(testNode) >> 1;
  assert(!IS_FREE(testNode));
  assert(GET_SIZE(testNode) == ~(1UL << (sizeof(size_t)*8 - 1)));

  // should accurately write frees
  SET_FREE(testNode, 1UL);
  assert(IS_FREE(testNode));
  SET_FREE(testNode, 0UL);
  assert(!IS_FREE(testNode));
  // should accurately write frees even if not 1 or 0
  SET_FREE(testNode, 1UL);
  assert(IS_FREE(testNode));

  // should accurately write sizes
  const size_t testSize = 5244;
  SET_SIZE(testNode, testSize);
  assert(GET_SIZE(testNode) == testSize);

  // should NOT overwrite size or free on writes to other
  SET_FREE(testNode, 0UL);
  const size_t anotherTestSize = 12;
  SET_SIZE(testNode, anotherTestSize);
  assert(!IS_FREE(testNode));
  SET_FREE(testNode, 1UL);
  assert(GET_SIZE(testNode) == anotherTestSize);
  const size_t yetAnotherTestSize = 8900444;
  SET_SIZE(testNode, yetAnotherTestSize);
  assert(IS_FREE(testNode));
  SET_FREE(testNode, 0UL);
  assert(GET_SIZE(testNode) == yetAnotherTestSize);

  /**
   * Block traversal
   */
  // set up some dummy values (previous ptr and size)
  SET_PREVIOUS_BLOCK(testNode, 0xbee71e5);
  SET_SIZE(testNode, ALIGN(testNodeSize));
  SET_FREE(testNode, 1UL);
  SET_NEXT_FREE_BLOCK(testNode, 0xf00d);
  SET_PREVIOUS_FREE_BLOCK(testNode, 0xba51c);
  // should properly find the next and previous blocks
  assert(GET_NEXT_BLOCK(testNode) == (char*)testNode + BLOCK_HEADER_SIZE + ALIGN(testNodeSize));
  assert(GET_PREVIOUS_BLOCK(testNode) == (block_node)0xbee71e5);
  assert(GET_NEXT_FREE_BLOCK(testNode) == (block_node)0xf00d);
  assert(GET_PREVIOUS_FREE_BLOCK(testNode) == (block_node)0xba51c);

  printf("Macros checked successfully!\n");

  return 1;
}

/*
 * Just a simple checker function to verify that no blocks
 * are the same as a block_node check.
 *
 * Returns 1 on uniqueness.
 *
 */
int check_unique(block_node check)
{
  block_node current = END;
  while(current != NULL) {
    if (check == current){
      return 0;
    }
    current = GET_PREVIOUS_BLOCK(current);
  }
  return 1;
}

/*
 * returns non-zero if heap is sensible.
 */
int mm_check(void)
{
int has_failed = 0;
  printf("HEAP START: %p; HEAP END: %p;\n", BASE, END);
  printf("BASE: %p; END: %p\n", BASE, END);
  size_t i;
  for( i = 0; i < FREE_LIST_COUNT; ++i) {
    printf("FREE_LIST[%lu]: %p\n",
      i,
      FREE_LIST[i]);
  }

  block_node current = BASE;
  block_node last = NULL;
  while((current != NULL) && (last < END)) {
    printf("\tCHECKING: %p; SIZE: %#lx; FREE: %d; NEXT: %p; PREVIOUS: %p\n",
      current,
      GET_SIZE(current),
      !!IS_FREE(current),
      GET_NEXT_BLOCK(current),
      GET_PREVIOUS_BLOCK(current));

    if(IS_FREE(current)) {
      printf("\t\tPOWER: %i; NEXTFREE: %p; PREVFREE: %p\n",
        GET_FREE_LIST_NUMBER(GET_SIZE(current)),
        GET_NEXT_FREE_BLOCK(current),
        GET_PREVIOUS_FREE_BLOCK(current));
    }
    // Check size of block
    if(GET_SIZE(current) < MIN_DATA_SIZE) {
      printf("Size error! %p size is %lu can't be less than %lu",
        current,
        GET_SIZE(current),
        MIN_DATA_SIZE);
        has_failed = 1;
    }
    // Check alignment of previous pointers
    if(last != GET_PREVIOUS_BLOCK(current)) {
      printf("Continuity error! %p doesn't pt to prev block %p, instead %p.\n",
        current,
        last,
        GET_PREVIOUS_BLOCK(current));
      has_failed = 1;
    }

    // Check contiguity of free space (with previous)
    if(last && IS_FREE(last) & IS_FREE(current)) {
      printf("Free error! %p is not connected to its free predecessor. \n",
        current);
      has_failed = 1;
    }

    // Make sure we're in the free list!
    if(IS_FREE(current)) {
      size_t power = GET_FREE_LIST_NUMBER(GET_SIZE(current));

      block_node node = FREE_LIST[power];
      while(node != NULL && node != current) {
        node = GET_NEXT_FREE_BLOCK(node);
      }

      if(!node) {
        printf("Free list error! %p is not in free list %lu.\n",
          current,
          power);

        printf("Would have NEXTFREE: %p; PREVFREE: %p\n",
          GET_NEXT_FREE_BLOCK(current), GET_PREVIOUS_FREE_BLOCK(current));
        has_failed = 1;
      }
    }

    last = current;
    current = GET_NEXT_BLOCK(current);
  }
  for( i = 0; i < FREE_LIST_COUNT; ++i) {
    current = FREE_LIST[i];
    last = NULL;
    while(current != NULL) {
      printf("\tFREE[%lu]CCK: %p; NEXT: %p; PREV: %p\n",
        i,
        current,
        GET_NEXT_FREE_BLOCK(current),
        GET_PREVIOUS_FREE_BLOCK(current));

      // Check for pointer alignment
      if(last != GET_PREVIOUS_FREE_BLOCK(current)) {
        printf("Continuity error! %p doesn't pt to prev block %p, instead %p.\n",
          current,
          last,
          GET_PREVIOUS_FREE_BLOCK(current));
        has_failed = 1;
      }

      last = current;
      current = GET_NEXT_FREE_BLOCK(current);
    }
  }

  if(!has_failed) {
    printf("Done mm_check!\n");
  } else {
    printf("!!!! Failed mm_check.\n");
    exit(1);
  }
  return !has_failed;
}

void *mem_sbrk(size_t size) {
  if (heap_start == NULL) {
    m_err = ERR_MEM_UNINITIALIZED;
    return (void*)-1;
  }
  heap_end = ((char*)heap_end) + size;
  #ifdef DEBUG
  #ifndef NDEBUG
    assert(heap_end >= heap_start);
  #endif
  #endif
  if ((heap_end - heap_start) > heap_size) {
    heap_end -= size;
    return (void *)-1;
  }
  return heap_end;
}

block_node request_space(block_node last, size_t size) {
  block_node block = mem_sbrk(0);
  void* request = mem_sbrk(GET_BLOCK_SIZE(size));

  if(request == (void*)-1) {
    return NULL;
  }
  #ifdef DEBUG
  #ifndef NDEBUG
    assert(request != NULL);
    assert(check_unique(block));
    assert(check_unique(request));
    assert((unsigned long)request % ALIGNMENT == 0);
  #endif
  #endif


  if (last) {
    SET_PREVIOUS_BLOCK(block, last);
  }
  SET_FREE(block, 1UL);
  SET_SIZE(block, (ALIGN(size)));

  //Bookkeeping
  END = block;

  return block;
}

int Mem_Init(int size) {

  #ifdef DEBUG
  #ifndef NDBUG
    printf("in debug mode\n");
    if(!macro_checker()) {
      return 0;
    }
  #endif
  #endif

  int page_size = getpagesize();
  // Rounds up the number of pages
  int num_pages = (size + (page_size-1))/page_size;
  int alloc_size = num_pages * page_size;

  heap_start = mmap(0, alloc_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
  if (heap_start == MAP_FAILED) {
    m_err = ERR_MEM_UNINITIALIZED;
    return -1;
  }
  heap_end = heap_start;
  heap_size = alloc_size;


  BASE =NULL;
  END = NULL;
  size_t i;
  for (i = 0; i < FREE_LIST_COUNT; ++i) {
    FREE_LIST[i] = NULL;
  }

  #ifdef DEBUG
  #ifndef NDEBUG
    mm_check();
  #endif
  #endif

  return 0;
}

/*
  find_free finds a free block, otherwise returns NULL.
*/
block_node find_free(size_t requestSize)
{
  requestSize = ALIGN(requestSize);

  // Round up the size to a power of 2.
  int power = GET_FREE_LIST_NUMBER(requestSize);
  // Find the lowest bin it could be in
  block_node current = FREE_LIST[power];
  while(current != NULL) {
    if (GET_SIZE(current) >= requestSize) {
      return current;
    }
    current = GET_NEXT_FREE_BLOCK(current);
  }

  // otherwise search the pper bins
  size_t i;
  for (i = power +1; i < FREE_LIST_COUNT; ++i) {
    if (FREE_LIST[i] != NULL) {
      return FREE_LIST[i];
    }
  }

  return NULL;
}

// Split a block
block_node split_block(block_node block, size_t splitSize) {
  #ifdef DEBUG
  #ifndef NDEBUG
    assert(splitSize < GET_SIZE(block) + BLOCK_HEADER_SIZE);
  #endif
  #endif

  splitSize = ALIGN(splitSize);

  if (GET_SIZE(block) - splitSize < GET_BLOCK_SIZE(0)) {
    return block;
  }
  int isEnd = (block == END);
  block_node nextBlock = NULL;
  if (!isEnd) {
    nextBlock = GET_NEXT_BLOCK(block);
  }

  size_t oldSize = GET_SIZE(block);
  SET_SIZE(block, splitSize);

  // Make new block
  block_node splitBlock = GET_NEXT_BLOCK(block);
  SET_PREVIOUS_BLOCK(splitBlock, block);
  SET_FREE(splitBlock, 1UL);
  SET_SIZE(splitBlock, (oldSize - (splitSize + BLOCK_HEADER_SIZE)));
  if (!isEnd) {
    SET_PREVIOUS_BLOCK(nextBlock, splitBlock);
  } else {
    END = splitBlock;
  }

  // add node to free list
  size_t power = GET_FREE_LIST_NUMBER(GET_SIZE(splitBlock));
  SET_NEXT_FREE_BLOCK(splitBlock, FREE_LIST[power]);
  SET_PREVIOUS_FREE_BLOCK(splitBlock, NULL);
  if (FREE_LIST[power]) {
    SET_PREVIOUS_FREE_BLOCK(FREE_LIST[power], splitBlock);
  }
  FREE_LIST[power] = splitBlock;

  return block;
}

void *Mem_Alloc(int size) {
  if (heap_start == NULL) {
    m_err = ERR_OUT_OF_SPACE;
    return NULL;
  }

  if (size <= 0) {
    return NULL;
  }

  block_node block = NULL;
  // If this is the first call, don't bother checking for a free block
  if(BASE) {
    block = find_free(size);
  }

  // If no free block, request space.
  if (!block) {
    block = request_space(END, GET_BLOCK_SIZE(size));
  } else {
    #ifdef DEBUG
    #ifndef NDEBUG
      assert((long)block % ALIGNMENT == 0);
    #endif
    #endif

    block_node prev = GET_PREVIOUS_FREE_BLOCK(block);
    block_node next = GET_NEXT_FREE_BLOCK(block);

    // If we have a previous, reconnect that. Otherwise we are at the head of the list
    if (prev) {
      SET_NEXT_FREE_BLOCK(prev, next);
    } else {
      size_t power = GET_FREE_LIST_NUMBER(GET_SIZE(block));
      #ifdef DEBUG
      #ifndef NDEBUG
        assert(FREE_LIST[power] == block);
      #endif
      #endif
      FREE_LIST[power] = next;
    }

    // If we have a next, reconnect to whatever it was before
    if (next) {
      SET_PREVIOUS_FREE_BLOCK(next, prev);
    }

    if (GET_SIZE(block) > (GET_BLOCK_SIZE(MIN_DATA_SIZE) + GET_BLOCK_SIZE(size))) {
      block = split_block(block, GET_BLOCK_SIZE(size));
    }

  }

  if (!block) {
    m_err = ERR_OUT_OF_SPACE;
    return NULL;
  }

  SET_FREE(block, 0UL);
  SET_NEXT_FREE_BLOCK(block, NULL);
  if (!BASE) {
    BASE = block;
  }

  #ifdef DEBUG
  #ifndef NDEBUG
    mm_check();
  #endif
  #endif

  return GET_DATA(block);
}

int Mem_Free(void *ptr)
{
  if(!ptr || ((unsigned long)ptr % ALIGNMENT != 0)) {
    m_err = ERR_INVALID_FREE;
    return -1;
  }

  block_node block = GET_HEADER(ptr);
  #ifdef DEBUG
  #ifndef NDEBUG
    assert((unsigned long)block %8 == 0);
  #endif
  #endif

  SET_FREE(block, 1UL);

  block_node prev = NULL;
  if (block != BASE) {
    prev = GET_PREVIOUS_BLOCK(block);
  }

  block_node next = NULL;
  if(block != END) {
    next = GET_NEXT_BLOCK(block);
  }

  // Coalesce left
  if (prev && IS_FREE(prev)) {
    #ifdef DEBUG
    #ifndef NDEBUG
      assert(GET_NEXT_BLOCK(prev) == block);
    #endif
    #endif

    size_t prevSize = GET_SIZE(prev);
    SET_SIZE(prev, (prevSize + GET_SIZE(block) + BLOCK_HEADER_SIZE));

    block_node prevFree = GET_PREVIOUS_FREE_BLOCK(prev);
    block_node nextFree = GET_NEXT_FREE_BLOCK(prev);

    if (prevFree) {
      SET_NEXT_FREE_BLOCK(prevFree, nextFree);
    } else {
      size_t power = GET_FREE_LIST_NUMBER(prevSize);
      #ifdef DEBUG
      #ifndef NDEBUG
        assert(FREE_LIST[power] == prev);
      #endif
      #endif
      FREE_LIST[power] = nextFree;
    }

    if (nextFree) {
      SET_PREVIOUS_FREE_BLOCK(nextFree, prevFree);
    }

    if (next) {
      SET_PREVIOUS_BLOCK(next, prev);
    } else {
      END = prev;
    }

    block = prev;
  }

  // Coalesce right
  if(next && IS_FREE(next)) {
    #ifdef DEBUG
    #ifndef NDEBUG
      assert(GET_PREVIOUS_BLOCK(next) == block);
    #endif
    #endif

    size_t currentSize = GET_SIZE(block);
    SET_SIZE(block, (currentSize + GET_SIZE(next) + BLOCK_HEADER_SIZE));

    block_node prevFree = GET_PREVIOUS_FREE_BLOCK(next);
    block_node nextFree = GET_NEXT_FREE_BLOCK(next);

    if(prevFree) {
      SET_NEXT_FREE_BLOCK(prevFree, nextFree);
    } else {
      size_t power = GET_FREE_LIST_NUMBER(GET_SIZE(next));
      #ifdef DEBUG
      #ifndef NDEBUG
        assert(FREE_LIST[power] == next);
      #endif
      #endif
      FREE_LIST[power] = nextFree;
    }

    // If we have a next node, connect tow what it was before
    if(nextFree) {
      SET_PREVIOUS_FREE_BLOCK(nextFree, prevFree);
    }

    if (next == END) {
      END = block;
    } else {
      SET_PREVIOUS_BLOCK(GET_NEXT_BLOCK(next), block);
    }
  }

  // Add node to free list.
  size_t power = GET_FREE_LIST_NUMBER(GET_SIZE(block));
  SET_NEXT_FREE_BLOCK(block, FREE_LIST[power]);
  SET_PREVIOUS_FREE_BLOCK(block, NULL);
  if (FREE_LIST[power]) {
    SET_PREVIOUS_FREE_BLOCK(FREE_LIST[power], block);
  }
  FREE_LIST[power] = block;

  #ifdef DEBUG
  #ifndef NDEBUG
    mm_check();
  #endif
  #endif

  return 0;
}

void Mem_Dump() {
  mm_check();
}
