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

static const unsigned int _BLOCK_SIZE =
		(unsigned int) ((long long) CACHE_LINE_SIZE - 3U * sizeof(void*));

static const unsigned int BLOCK_SIZE = _BLOCK_SIZE / sizeof(void*) * sizeof(void*);

class Memory;

struct Info {
	int size;
};

static const unsigned int INFO_SIZE = ((unsigned int) (sizeof(Info)
		+ (unsigned int) ((long long) MAX_ALIGNMENT - 1))) / MAX_ALIGNMENT
		* MAX_ALIGNMENT;

class MemoryNode {
private:
	unsigned char block[BLOCK_SIZE];
	MemoryNode* volatile next;
	unsigned char* volatile head;
	unsigned char* volatile tail;

public:
	inline MemoryNode(MemoryNode* next);
	inline virtual ~MemoryNode();

	inline bool allocate(unsigned char*& address, unsigned int size);
	inline bool deallocate(unsigned char*& address);

	inline bool isTooSmall() const;
	inline void reset();

	friend class Memory;
};

class Memory {
private:
	MemoryNode DUMMY;
	MemoryNode* volatile tail;
	MemoryNode* volatile reserved;

public:
	Memory();
	virtual ~Memory();

	template<typename Type> void allocate(Type*& item);
	template<typename Type> void deallocate(Type*& item);

private:
	void deallocateHelper(MemoryNode* headOld);
};

MemoryNode::MemoryNode(MemoryNode* next) :
		next(next), head(block), tail(block) {
	bzero(block, BLOCK_SIZE);
	__sync_synchronize();
}

MemoryNode::~MemoryNode() {
	reset();
	this->next = 0;
	__sync_synchronize();
}

bool MemoryNode::allocate(unsigned char*& address, unsigned int size) {
	size += INFO_SIZE;

	while (true) {
		if (this->block + BLOCK_SIZE <= this->tail + size) {
			address = 0;
			return false;
		}

		unsigned char* tailOld = this->tail;
		if (__sync_bool_compare_and_swap(&this->tail, tailOld,
				tailOld + size)) {
			((Info*) tailOld)->size = size;
			address = tailOld + INFO_SIZE;
			return true;
		}
	}
}

bool MemoryNode::deallocate(unsigned char*& address) {
	address -= INFO_SIZE;
	return __sync_bool_compare_and_swap(&this->head, address,
			address + ((Info*) address)->size);
}

bool MemoryNode::isTooSmall() const {
	return this->head + sizeof(int) >= this->tail;
}

void MemoryNode::reset() {
	bzero(block, BLOCK_SIZE);
	this->tail = block;
	this->head = block;
	__sync_synchronize();
}

Memory::Memory() :
		DUMMY(0), tail(&DUMMY), reserved(0) {
	__sync_synchronize();
}

Memory::~Memory() {
	while (true) {
		MemoryNode* reserved = this->reserved;
		if (!reserved) {
			__sync_synchronize();
			if (!reserved) {
				break;
			}
		}
		__sync_bool_compare_and_swap(&this->reserved, reserved, reserved->next);
		delete reserved;
	}
	this->tail = &DUMMY;
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
		unsigned char* address = 0;
		if (&DUMMY != tailOld && tailOld->allocate(address, sizeof(Type))) {
			if (tailNew) {
				delete tailNew;
				tailNew = 0;
			}
			data = new (address) Type();
			return;
		}

		if (!tailNew) {
			tailNew = new MemoryNode(0);
		}
		if (__sync_bool_compare_and_swap(&tailOld->next, 0, tailNew)) {
			tailNew->allocate(address, sizeof(Type));
			data = new (address) Type();
			__sync_bool_compare_and_swap(&this->tail, tailOld, tailNew);
			__sync_bool_compare_and_swap(&this->reserved, 0, tailNew);
			return;
		} else {
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
	unsigned char* address = (unsigned char*) data;

	while (true) {
		MemoryNode* headOld = this->DUMMY.next;
		if (headOld == 0) {
			__sync_synchronize();
			headOld = this->DUMMY.next;
			if (headOld == 0)
				return;
		}

		if (headOld->deallocate(address)) {
			if (headOld->isTooSmall())
				deallocateHelper(headOld);
			return;
		}

		deallocateHelper(headOld);
	}
}

void Memory::deallocateHelper(MemoryNode* headOld) {
	MemoryNode* headNew = headOld->next;
	if (__sync_bool_compare_and_swap(&this->DUMMY.next, headOld, headNew)) {
		if (!headNew) {
			if (this->reserved) {
				if (!__sync_bool_compare_and_swap(&this->tail, headOld,
						this->reserved)) {
					__sync_bool_compare_and_swap(&this->DUMMY.next, 0,
							headOld->next);
				}
			} else {
				if (!__sync_bool_compare_and_swap(&this->tail, headOld,
						&this->DUMMY)) {
					__sync_bool_compare_and_swap(&this->DUMMY.next, 0,
							headOld->next);
				}
			}
		}
		headOld->reset();
	}
}

#endif /* INC_MEMORY_H_ */
