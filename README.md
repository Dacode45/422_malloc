# 422_malloc

mem.c implments a memory allocator.
The memory allocator is first fit with free list buckets.

Program also works on 32 bit machines, set ALIGNMENT=4

We define a block_node as an 8 byte aligned block of memory divided in 3 sections.

The first section is a void* that contains the previous block_node.
The second section is a size_t. If the upper bit is 1, the block_node is free. Else it is not.
The third section is the data section. When the block_node is free, this section contains a void * to the previous block_node in the free list, and a void * to the next block_node in the free list.

20 free list are maintained. block_nodes are put into the free lists based on their size. The log base 2 of a block_node's size is the free list they are put into.

The memory allocator first checks if there is a block_node in the free list of the size specified. If not, it finds the first non empty free_list for sizes greater than that given, and splits the block. 
