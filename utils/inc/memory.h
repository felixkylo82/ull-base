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

#define CACHE_LINE_COUNT_MEMORY_NODE 128U

static const unsigned int BLOCK_SIZE = CACHE_LINE_COUNT_MEMORY_NODE * CACHE_LINE_SIZE;
static const unsigned int BLOCK_COUNT = BLOCK_SIZE / sizeof(unsigned int);
//static const unsigned int LOT_COUNT = BLOCK_COUNT / sizeof(unsigned int);

class Memory;

struct Info {
	unsigned int next;
};

static const unsigned int INFO_SIZE = (sizeof(Info) + (unsigned int) ((long long) MAX_ALIGNMENT - 1)) / MAX_ALIGNMENT * MAX_ALIGNMENT;
static const unsigned int INFO_COUNT = INFO_SIZE / sizeof(unsigned long long);

class MemoryNode {
private:
	volatile unsigned int tail;
	volatile unsigned int isFull;
	unsigned int blocks[BLOCK_COUNT] __attribute__ ((aligned (CACHE_LINE_SIZE)));
	//unsigned char lots[BLOCK_COUNT] __attribute__ ((aligned (CACHE_LINE_SIZE)));
	MemoryNode* volatile next __attribute__ ((aligned (CACHE_LINE_SIZE)));
	volatile unsigned int head __attribute__ ((aligned (CACHE_LINE_SIZE)));

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
	MemoryNode* volatile dummy __attribute__ ((aligned (CACHE_LINE_SIZE)));
	MemoryNode* volatile tail __attribute__ ((aligned (CACHE_LINE_SIZE)));

	MemoryNode* volatile dummyReserved __attribute__ ((aligned (CACHE_LINE_SIZE)));
	MemoryNode* volatile tailReserved __attribute__ ((aligned (CACHE_LINE_SIZE)));

public:
	Memory(unsigned int preAlllocatedSize = 1024U * 1024U);
	virtual ~Memory();

	template<typename Type> void allocate(Type*& item);
	template<typename Type> void deallocate(Type*& item);

private:
	void deallocateHelper(MemoryNode* dummy, MemoryNode* headOld);

	void pushReserved(MemoryNode*& item);
	MemoryNode* popReserved();
};

MemoryNode::MemoryNode(MemoryNode* next) :
		tail(0), isFull(0U), next(next), head(0) {
	ASSERT(BLOCK_SIZE == sizeof(blocks), "CRITICAL ERROR!");
	bzero(blocks, sizeof(blocks));
	//bzero(lots, sizeof(lots));
	__sync_synchronize();
}

MemoryNode::~MemoryNode() {
	reset();
	__sync_synchronize();
}

bool MemoryNode::allocate(unsigned int*& _address, unsigned int size) {
	size = (size + INFO_SIZE + (unsigned int) ((long long) MAX_ALIGNMENT - 1)) / MAX_ALIGNMENT * MAX_ALIGNMENT;
	unsigned int count = size / sizeof(unsigned int);

	while (true) {
		unsigned int tailOld = this->tail;
		if (BLOCK_COUNT <= tailOld + count) {
			_address = 0;
			this->isFull = 1U;
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
			info->next = this->tail;
			//lots[tailOld] = 1U;
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

	Info* info = ((Info*) (blocks + address));
	if (!__sync_bool_compare_and_swap(&this->head, address, info->next)) {
		ASSERT(false, "unordered deallocations are not supported");
	}
	return true;
}

bool MemoryNode::isFree() const {
	return this->head >= this->tail;
}

void MemoryNode::reset() {
	bzero(blocks, sizeof(blocks));
	//bzero(lots, sizeof(lots));
	this->next = 0;
	this->tail = 0;
	this->head = 0;
	this->isFull = 0U;
	__sync_synchronize();
}

Memory::Memory(unsigned int preAlllocatedSize) :
		dummy(new MemoryNode(0)), tail(dummy), dummyReserved(new MemoryNode(0)), tailReserved(this->dummyReserved) {
	dummy->isFull = 1U;

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

	this->tailReserved = 0;
	while(true) {
		MemoryNode* dummyReserved = this->dummyReserved;
		if (!dummyReserved) {
			__sync_synchronize();
			dummyReserved = this->dummyReserved;
			if (!dummyReserved) {
				break;
			}
		}

		__sync_bool_compare_and_swap(&this->dummyReserved, dummyReserved, dummyReserved->next);
		dummyReserved->next = 0;
		delete dummyReserved;
		dummyReserved = 0;
	}

	this->tail = 0;
	while(true) {
		MemoryNode* dummy = this->dummy;
		if (!dummy) {
			__sync_synchronize();
			dummy = this->dummy;
			if (!dummy) {
				break;
			}
		}

		__sync_bool_compare_and_swap(&this->dummy, dummy, dummy->next);
		dummy->next = 0;
		delete dummy;
		dummy = 0;
	}

	__sync_synchronize();
}

template<typename Type>
void Memory::allocate(Type*& data) {
	if (sizeof(Type) + INFO_SIZE > BLOCK_SIZE) {
		data = new Type();
		return;
	}

	MemoryNode* tailNew = 0;
	unsigned int* address = 0;

	while (true) {
		MemoryNode* tailOld = this->tail;

		while (tailOld->next) {
			__sync_bool_compare_and_swap(&this->tail, tailOld, tailOld->next);
			tailOld = this->tail;
		}

		if (tailOld->allocate(address, sizeof(Type))) {
			if (tailNew) {
				this->pushReserved(tailNew);
			}
			data = new (address) Type();
			return;
		}

		if (!tailNew) {
			tailNew = this->popReserved();
			tailNew->allocate(address, sizeof(Type));
		}
		if (__sync_bool_compare_and_swap(&tailOld->next, 0, tailNew)) {
			__sync_bool_compare_and_swap(&this->tail, tailOld, tailNew);
			data = new (address) Type();
			return;
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
		MemoryNode* dummy = this->dummy;
		MemoryNode* headOld = dummy->next;
		if (!headOld) {
			__sync_synchronize();
			dummy = this->dummy;
			headOld = dummy->next;
			if (!headOld) {
				ASSERT(false, "unordered deallocations are not supported");
				return;
			}
		}

		if (headOld->deallocate(address)) {
			if (headOld->isFree())
				deallocateHelper(dummy, headOld);
			return;
		}

		deallocateHelper(dummy, headOld);
	}
}

void Memory::deallocateHelper(MemoryNode* dummy, MemoryNode* headOld) {
	if (!headOld->isFull) {
		__sync_synchronize();
		if (!headOld->isFull)
			return;
	}

	if (__sync_bool_compare_and_swap(&this->dummy, dummy, headOld)) {
		this->pushReserved(dummy);
	}
}

void Memory::pushReserved(MemoryNode*& tailNew) {
	tailNew->next = 0;
	//tailNew->reset();

	while (true) {
		MemoryNode* tailOld = this->tailReserved;
		while (tailOld->next) {
			__sync_bool_compare_and_swap(&this->tailReserved, tailOld, tailOld->next);
			tailOld = this->tailReserved;
		}

		if (__sync_bool_compare_and_swap(&tailOld->next, 0, tailNew)) {
			__sync_bool_compare_and_swap(&this->tailReserved, tailOld, tailNew);
			tailNew = 0;
			return;
		}
	}
}

MemoryNode* Memory::popReserved() {
	while (true) {
		MemoryNode* dummyReserved = this->dummyReserved;
		MemoryNode* headOld = dummyReserved->next;
		if (headOld == 0) {
			__sync_synchronize();
			dummyReserved = this->dummyReserved;
			headOld = dummyReserved->next;
			if (headOld == 0)
				return new MemoryNode(0);
		}

		if (__sync_bool_compare_and_swap(&this->dummyReserved, dummyReserved, headOld)) {
			dummyReserved->reset();
			return dummyReserved;
		}
	}
}

#endif /* INC_MEMORY_H_ */
