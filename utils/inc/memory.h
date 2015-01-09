/*
 * memory.h
 *
 *  Created on: Jan 7, 2015
 *      Author: felix
 */

#ifndef INC_MEMORY_H_
#define INC_MEMORY_H_

#include "common.h"

#include <strings.h>
#include <new>

#define CACHE_LINE_COUNT_MEMORY_NODE 64U

static const unsigned int BLOCK_SIZE = CACHE_LINE_COUNT_MEMORY_NODE * CACHE_LINE_SIZE / sizeof(void*) * sizeof(void*);
static const unsigned int BLOCK_COUNT = BLOCK_SIZE / sizeof(unsigned int);

class Memory;

struct Info {
	unsigned int size;
	unsigned char isAllocated;
};

static const unsigned int INFO_SIZE = (sizeof(Info) + (unsigned int) ((long long) MAX_ALIGNMENT - 1)) / MAX_ALIGNMENT * MAX_ALIGNMENT;
static const unsigned int INFO_COUNT = INFO_SIZE / sizeof(unsigned int);

class MemoryNode {
private:
	unsigned int blocks[BLOCK_COUNT];
	MemoryNode* volatile next;
	volatile unsigned int head;
	volatile unsigned int tail;
	volatile unsigned char isFull;

public:
	inline MemoryNode(MemoryNode* next);
	inline virtual ~MemoryNode();

	inline bool allocate(unsigned int*& address, unsigned int size);
	inline bool deallocate(unsigned int*& address);

	inline bool isFree() const;
	inline void reset();

	friend class Memory;
};

class Memory {
private:
	MemoryNode* volatile dummy;
	MemoryNode* volatile tail;
	MemoryNode* volatile reserved;

public:
	Memory(unsigned int preAlllocatedSize = CACHE_LINE_SIZE * 64);
	virtual ~Memory();

	template<typename Type> void allocate(Type*& item);
	template<typename Type> void deallocate(Type*& item);

private:
	void deallocateHelper(MemoryNode* headOld);
};

MemoryNode::MemoryNode(MemoryNode* next) :
		next(next), head(0), tail(0), isFull(0) {
	bzero(blocks, BLOCK_SIZE);
	__sync_synchronize();
}

MemoryNode::~MemoryNode() {
	reset();
	this->next = 0;
	__sync_synchronize();
}

bool MemoryNode::allocate(unsigned int*& _address, unsigned int size) {
	size = (size + INFO_SIZE + (unsigned int) ((long long) sizeof(unsigned int) - 1)) / sizeof(unsigned int) * sizeof(unsigned int);
	unsigned int count = size / sizeof(unsigned int);

	while (true) {
		unsigned int tailOld = this->tail;
		if (BLOCK_COUNT <= tailOld + count) {
			_address = 0;
			this->isFull = 1;
			__sync_synchronize();
			return false;
		}

		if (this->isFull)
			return false;
		__sync_synchronize();
		if (this->isFull)
			return false;

		if (__sync_bool_compare_and_swap(&this->tail, tailOld, tailOld + count)) {
			Info* info = (Info*) (blocks + tailOld);
			info->size = size;
			info->isAllocated = 1;
			_address = blocks + tailOld + INFO_COUNT;
			return true;
		}
	}
}

bool MemoryNode::deallocate(unsigned int*& _address) {
	unsigned int address = _address - blocks;
	address -= INFO_COUNT;
	if (address < 0 || address >= BLOCK_COUNT) {
		return false;
	}

	Info* info = (Info*) (blocks + address);
	info->isAllocated = 0;

	while (true) {
		unsigned int head = this->head;
		if (head >= this->tail)
			break;
		Info* info = ((Info*) (blocks + head));
		if (info->isAllocated) {
			__sync_synchronize();
			head = this->head;
			if (head >= this->tail)
				break;
			info = ((Info*) (blocks + head));
			if (info->isAllocated)
				break;
		}

		unsigned int count = info->size / sizeof(unsigned int);

		__sync_bool_compare_and_swap(&this->head, head, head + count);
	}
	return true;
}

bool MemoryNode::isFree() const {
	return this->head >= this->tail;
}

void MemoryNode::reset() {
	bzero(blocks, BLOCK_SIZE);
	this->tail = 0;
	this->head = 0;
	this->isFull = 0;
	__sync_synchronize();
}

Memory::Memory(unsigned int preAlllocatedSize) :
		dummy(new MemoryNode(0)), tail(dummy), reserved(0) {
	preAlllocatedSize += (unsigned int) ((long long) sizeof(double) - 1);
	unsigned int count = preAlllocatedSize / sizeof(double);
	double** buffers = new double*[count];
	for (unsigned int i = 0; i < count; ++i)
		this->allocate(buffers[i]);
	for (unsigned int i = 0; i < count; ++i)
		this->deallocate(buffers[i]);
	delete[] buffers;
	__sync_synchronize();
}

Memory::~Memory() {
	while (true) {
		MemoryNode* reserved = this->reserved;
		if (!reserved) {
			__sync_synchronize();
			reserved = this->reserved;
			if (!reserved) {
				break;
			}
		}

		if ((void *) -1 == reserved->next) {
			__sync_synchronize();
			if ((void *) -1 == reserved->next) {
				continue;
			}
		}

		__sync_bool_compare_and_swap(&this->reserved, reserved, reserved->next);
		delete reserved;
	}
	this->tail = dummy;
	__sync_synchronize();
}

template<typename Type>
void Memory::allocate(Type*& data) {
	if (sizeof(Type) + INFO_SIZE > BLOCK_SIZE) {
		data = new Type();
		return;
	}

	MemoryNode* tailNew = 0;
	while (true) {
		MemoryNode* tailOld = this->tail;

		unsigned int* address = 0;
		if (dummy != tailOld && tailOld->allocate(address, sizeof(Type))) {
			if (tailNew) {
				delete tailNew;
				tailNew = 0;
			}
			data = new (address) Type();
			return;
		}

		if (__sync_bool_compare_and_swap(&tailOld->next, 0, (void *) -1)) {
			if (dummy == tailOld || (dummy->next != tailOld || !tailOld->isFree())) {
				__sync_synchronize();
				if (dummy == tailOld || (dummy->next != tailOld || !tailOld->isFree())) {
					if (!tailNew) {
						tailNew = new MemoryNode(0);
						tailNew->allocate(address, sizeof(Type));
					}
					__sync_bool_compare_and_swap(&this->tail, tailOld, tailNew);
					__sync_bool_compare_and_swap(&this->reserved, 0, tailNew);
					__sync_bool_compare_and_swap(&tailOld->next, (void *) -1, tailNew);
					data = new (address) Type();
					return;
				}
			}

			if (this->reserved) {
				__sync_bool_compare_and_swap(&this->tail, tailOld, this->reserved);
				this->dummy->next = this->reserved;
				__sync_bool_compare_and_swap(&tailOld->next, (void *) -1, 0);
				if (tailNew) {
					delete tailNew;
					tailNew = 0;
				}
				this->reserved->allocate(address, sizeof(Type));
				data = new (address) Type();
				return;
			}

			__sync_bool_compare_and_swap(&tailOld->next, (void *) -1, 0);
		} else {
			if (tailOld->next && (void*) -1 != tailOld->next)
				__sync_bool_compare_and_swap(&this->tail, tailOld, tailOld->next);
		}
	}
}

template<typename Type>
void Memory::deallocate(Type*& data) {
	if (sizeof(Type) + INFO_SIZE > BLOCK_SIZE) {
		delete data;
		data = 0;
		return;
	}

	data->~Type();
	unsigned int* address = (unsigned int*) data;

	while (true) {
		MemoryNode* headOld = this->dummy->next;
		if (!headOld || (void *) -1 == headOld) {
			__sync_synchronize();
			headOld = this->dummy->next;
			if (!headOld || (void *) -1 == headOld)
				return;
		}

		if (headOld->deallocate(address)) {
			if (headOld->isFree())
				deallocateHelper(headOld);
			return;
		}

		deallocateHelper(headOld);
	}
}

void Memory::deallocateHelper(MemoryNode* headOld) {
	if (!headOld->isFull) {
		__sync_synchronize();
		if (!headOld->isFull)
			return;
	}

	MemoryNode* headNew = headOld->next;
	if (!headNew || (void *) -1 == headNew) {
		__sync_synchronize();
		if (!headNew || (void *) -1 == headNew)
			return;
	}

	if (__sync_bool_compare_and_swap(&this->dummy->next, headOld, headNew)) {
		headOld->reset();
	}
}

#endif /* INC_MEMORY_H_ */
