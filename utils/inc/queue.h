/*
 * queue.h
 *
 *  Created on: Jan 7, 2015
 *      Author: felix
 */

#ifndef INC_QUEUE_H_
#define INC_QUEUE_H_

#include "memory.h"

#include <strings.h>

#define CACHE_LINE_COUNT_QUEUE_NODE CACHE_LINE_COUNT_MEMORY_NODE / 8U

static const unsigned int ITEM_COUNT = CACHE_LINE_COUNT_QUEUE_NODE * CACHE_LINE_SIZE / sizeof(void*);

template<typename Item>
class Queue;

template<typename Item>
class QueueNode {
private:
	Item* items[ITEM_COUNT];
	QueueNode<Item>* volatile next;
	volatile unsigned int head;
	volatile unsigned int tail;

public:
	inline QueueNode();
	inline virtual ~QueueNode();

	inline bool push(Item* item);
	inline bool pop(Item*& item);

	inline bool isFull() const;

	friend class Queue<Item> ;
};

template<typename Item>
class Queue {
private:
	Memory memory;
	QueueNode<Item>* volatile dummy;
	QueueNode<Item>* volatile tail;

public:
	Queue();
	virtual ~Queue();

	void push(Item* item);
	Item* pop();
};

template<typename Item>
QueueNode<Item>::QueueNode() :
		next(0), head(0), tail(0) {
	bzero(items, sizeof(items));
	__sync_synchronize();
}

template<typename Item>
QueueNode<Item>::~QueueNode() {
	bzero(items, sizeof(items));
	this->tail = 0;
	this->head = 0;
	this->next = 0;
	__sync_synchronize();
}

template<typename Item>
bool QueueNode<Item>::push(Item* item) {
	while (true) {
		unsigned int tailOld = this->tail;
		if (ITEM_COUNT * 2 <= tailOld + 1) {
			return false;
		}

		if (0 == tailOld % 2 && __sync_bool_compare_and_swap(&this->tail, tailOld, tailOld + 1)) {
			items[tailOld / 2] = item;
			__sync_fetch_and_add(&this->tail, 1);
			return true;
		}
	}
}

template<typename Item>
bool QueueNode<Item>::pop(Item*& item) {
	while (true) {
		unsigned int headOld = this->head;
		if (this->tail <= headOld + 1) {
			__sync_synchronize();
			headOld = this->head;
			if (this->tail <= headOld + 1) {
				item = 0;
				return false;
			}
		}

		if (__sync_bool_compare_and_swap(&this->head, headOld, headOld + 2)) {
			item = items[headOld / 2];
			return true;
		}
	}
}

template<typename Item>
bool QueueNode<Item>::isFull() const {
	return ITEM_COUNT * 2 <= this->tail;
}

template<typename Item>
Queue<Item>::Queue() :
		dummy(new QueueNode<Item>()), tail(dummy) {
	__sync_synchronize();
}

template<typename Item>
Queue<Item>::~Queue() {
	while (this->pop())
		;
	tail = 0;
	dummy = 0;
	__sync_synchronize();
}

template<typename Item>
void Queue<Item>::push(Item* item) {
	QueueNode<Item>* tailNew = 0;
	while (true) {
		QueueNode<Item>* tailOld = this->tail;
		while (tailOld->next) {
			__sync_bool_compare_and_swap(&this->tail, tailOld, tailOld->next);
			tailOld = this->tail;
		}

		if (dummy != tailOld && tailOld->push(item)) {
			if (tailNew) {
				memory.deallocate(tailNew);
				//delete tailNew;
				//tailNew = 0;
			}
			return;
		}

		if (!tailNew) {
			memory.allocate(tailNew);
			//tailNew = new QueueNode<Item>();
			tailNew->push(item);
		}
		if (__sync_bool_compare_and_swap(&this->tail, tailOld, tailNew)) {
			__sync_bool_compare_and_swap(&tailOld->next, 0, tailNew);
			return;
		}
	}
}

template<typename Item>
Item* Queue<Item>::pop() {
	while (true) {
		QueueNode<Item>* headOld = this->dummy->next;
		if (headOld == 0) {
			__sync_synchronize();
			headOld = this->dummy->next;
			if (headOld == 0)
				return 0;
		}

		Item* item = 0;
		if (headOld->pop(item)) {
			return item;
		}

		if (!headOld->isFull()) {
			__sync_synchronize();
			if (!headOld->isFull())
				return 0;
		}

		if (headOld->pop(item)) {
			return item;
		}

		QueueNode<Item>* headNew = headOld->next;
		if (!headNew) {
			__sync_synchronize();
			headNew = headOld->next;
			if (!headNew)
				return 0;
		}

		if (__sync_bool_compare_and_swap(&this->dummy->next, headOld, headNew)) {
			headOld->next = 0;
			memory.deallocate(headOld);
			//delete headOld;
			//headOld = 0;
		}
	}
}

#endif /* INC_QUEUE_H_ */
