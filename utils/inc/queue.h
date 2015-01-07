/*
 * queue.h
 *
 *  Created on: Jan 7, 2015
 *      Author: felix
 */

#ifndef INC_QUEUE_H_
#define INC_QUEUE_H_

#include "common.h"

#include <strings.h>

static const unsigned int __ITEM_COUNT = (CACHE_LINE_SIZE - sizeof(void*)
		- 2 * sizeof(unsigned char)) / sizeof(void*);
static const unsigned int ITEM_COUNT = (0 == __ITEM_COUNT ? 1 : __ITEM_COUNT);

template<typename Item>
class Queue;

template<typename Item>
class QueueNode {
private:
	Item* items[ITEM_COUNT];
	QueueNode<Item>* volatile next;
	volatile unsigned char head;
	volatile unsigned char tail;

public:
	inline QueueNode(QueueNode<Item>* next);
	inline virtual ~QueueNode();

	inline bool push(Item* item);
	inline bool pop(Item*& item);

	friend class Queue<Item> ;
};

template<typename Item>
class Queue {
private:
	QueueNode<Item> DUMMY;
	QueueNode<Item>* volatile tail;

public:
	Queue();
	virtual ~Queue();

	void push(Item* item);
	Item* pop();
};

template<typename Item>
QueueNode<Item>::QueueNode(QueueNode<Item>* next) :
		next(next), head(0), tail(0) {
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
		if (ITEM_COUNT * 2 <= this->tail) {
			return false;
		}

		unsigned char tailOld = this->tail;
		if (__sync_bool_compare_and_swap(&this->tail, tailOld, tailOld + 1)) {
			items[tailOld / 2] = item;
			__sync_fetch_and_add(&this->tail, 1);
			return true;
		}
	}
}

template<typename Item>
bool QueueNode<Item>::pop(Item*& item) {
	while (true) {
		if (this->tail <= this->head + 1) {
			__sync_synchronize();
			if (this->tail <= this->head + 1) {
				item = 0;
				return false;
			}
		}

		unsigned char headOld = this->head;
		if (__sync_bool_compare_and_swap(&this->head, headOld, headOld + 2)) {
			item = items[headOld / 2];
			return true;
		}
	}
}

template<typename Item>
Queue<Item>::Queue() :
		DUMMY(0), tail(&DUMMY) {
	__sync_synchronize();
}

template<typename Item>
Queue<Item>::~Queue() {
	__sync_synchronize();
}

template<typename Item>
void Queue<Item>::push(Item* item) {
	QueueNode<Item>* tailNew = 0;
	while (true) {
		QueueNode<Item>* tailOld = this->tail;
		if (&DUMMY != tailOld && tailOld->push(item)) {
			if (tailNew) {
				delete tailNew;
				tailNew = 0;
			}
			return;
		}

		if (!tailOld->next) {
			if (!tailNew) {
				tailNew = new QueueNode<Item>(0);
			}
			if (__sync_bool_compare_and_swap(&tailOld->next, 0, tailNew)) {
				tailNew->push(item);
				__sync_bool_compare_and_swap(&this->tail, tailOld, tailNew);
				return;
			}
		}
		__sync_synchronize();
	}
}

template<typename Item>
Item* Queue<Item>::pop() {
	while (true) {
		QueueNode<Item>* headOld = this->DUMMY.next;
		if (headOld == 0) {
			__sync_synchronize();
			headOld = this->DUMMY.next;
			if (headOld == 0)
				return 0;
		}

		Item* item = 0;
		if (headOld->pop(item)) {
			return item;
		}

		QueueNode<Item>* headNew = headOld->next;
		if (__sync_bool_compare_and_swap(&this->DUMMY.next, headOld, headNew)) {
			if (!headNew) {
				if (!__sync_bool_compare_and_swap(&this->tail, headOld,
						&this->DUMMY)) {
					__sync_bool_compare_and_swap(&this->DUMMY.next, 0,
							headOld->next);
				}
			}
			headOld->next = 0;
			delete headOld;
			headOld = 0;
		}
	}
}

#endif /* INC_QUEUE_H_ */
