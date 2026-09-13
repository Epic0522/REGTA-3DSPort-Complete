/* perhaps we can improve texture allocation? */

// extern "C"
// {
// 	#include <3ds/types.h>
// 	#include <3ds/allocator/linear.h>
// 	#include <3ds/util/rbtree.h>
// }

// #include "linear/mem_pool.h"
// #include "linear/addrmap.h"

// extern u32 __ctru_linear_heap;
// extern u32 __ctru_linear_heap_size;

// static MemPool sMyLinearPool;

// static bool
// myLinearInit()
// {
// 	auto blk = MemBlock::Create((u8*)__ctru_linear_heap, __ctru_linear_heap_size);
// 	if (blk)
// 	{
// 		sMyLinearPool.AddBlock(blk);
// 		rbtree_init(&sAddrMap, addrMapNodeComparator);
// 		return true;
// 	}
// 	return false;
// }

// void*
// myLinearMemAlign(size_t size, size_t alignment)
// {
// 	// Enforce minimum alignment
// 	if (alignment < 16)
// 		alignment = 16;

// 	// Convert alignment to shift amount
// 	int shift;
// 	for (shift = 4; shift < 32; shift ++)
// 	{
// 		if ((1U<<shift) == alignment)
// 			break;
// 	}
// 	if (shift == 32) // Invalid alignment
// 		return nullptr;

// 	// Initialize the pool if it is not ready
// 	if (!sMyLinearPool.Ready() && !myLinearInit())
// 		return nullptr;

// 	// Allocate the chunk
// 	MemChunk chunk;
// 	if (!sMyLinearPool.Allocate(chunk, size, shift))
// 		return nullptr;

// 	auto node = newNode(chunk);
// 	if (!node)
// 	{
// 		sMyLinearPool.Deallocate(chunk);
// 		return nullptr;
// 	}
// 	if (rbtree_insert(&sAddrMap, &node->node));
// 	return chunk.addr;
// }

// void*
// myLinearAlloc(size_t size)
// {
// 	return myLinearMemAlign(size, 0x80);
// }

// void*
// myLinearRealloc(void* mem, size_t size)
// {
// 	// TODO
// 	return NULL;
// }

// size_t
// myLinearGetSize(void* mem)
// {
// 	auto node = getNode(mem);
// 	return node ? node->chunk.size : 0;
// }

// void
// myLinearFree(void* mem)
// {
// 	auto node = getNode(mem);
// 	if (!node) return;

// 	// Free the chunk
// 	sMyLinearPool.Deallocate(node->chunk);

// 	// Free the node
// 	delNode(node);
// }

// /* free the first 'size' bytes of a buffer. */
// // void*
// // myLinearPreFree(void *mem, size_t size)
// // {
// // 	auto   old_node = getNode(mem);
// // 	uint8  *old_ptr = old_node->addr;
// // 	size_t old_size = old_node->size;

// // 	// Free the chunk
// // 	sMyLinearPool.Deallocate(node->chunk);

// // 	// Remove the node
// // 	delNode(node);

// // 	// Alter node
// // 	node.addr += size;
// // 	node.size -= size;

// // 	auto new_node = newNode(chunk);
// // 	new_node->addr = old_ptr + size;
// // 	new_node->size = old_size - size;

// // 	// Reinsert the node
// // 	if (rbtree_insert(&sAddrMap, &new_node->node));

// // 	return chunk.addr;
// // }

// u32
// myLinearSpaceFree()
// {
// 	return sMyLinearPool.GetFreeSpace();
// }
