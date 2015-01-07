/*
 * queue.h
 *
 *  Created on: Jan 7, 2015
 *      Author: felix
 */

#ifndef INC_QUEUE_H_
#define INC_QUEUE_H_

#include "common.h"

static const unsigned int __ITEM_COUNT = (CACHE_LINE_SIZE - sizeof(void*) - 2 * sizeof(unsigned char)) / sizeof(void*);
static const unsigned int ITEM_COUNT = (0 == __ITEM_COUNT ? 1 : __ITEM_COUNT);

template<typename Item>
class QueueNode {
private:
    Item* items[ITEM_COUNT];
    QueueNode* volatile next;
    volatile unsigned char head;
    volatile unsigned char tail;
    
public:
    inline QueueNode(QueueNode<Item>* next);
    inline virtual ~QueueNode();

    inline bool push(Item* item);
    inline bool pop(Item*& item);
};

template<typename Item>
class Queue {
private:
	volatile QueueNode<Item> DUMMY;
	QueueNode<Item>* volatile tail;
	
public:
    Queue();
    virtual ~Queue();
    
    void push(Item* item);
    Item* pop();
};


template<typename Item>
QueueNode<Item>::QueueNode(QueueNode<Item>* next) : next(next) {
}

template<typename Item>
QueueNode<Item>::~QueueNode() {
    this->next = 0;
}

template<typename Item>
Queue<Item>::Queue() : tail(&DUMMY) {
}

template<typename Item>
Queue<Item>::~Queue() {
    this->tail = 0;
}

template<typename Item>
void Queue<Item>::push(Item* item) {
    QueueNode<Item>* tailNew = new QueueNode<Item>(item, 0);
    while(true) {
        QueueNode<Item>* tailOld = this->tail;
        if(!tailOld->next) {
            if(__sync_bool_compare_and_swap(&tailOld->next, 0, tailNew)) {
                __sync_bool_compare_and_swap(&this->tail, tailOld, tailNew);
                return;
            }
        }
        __sync_synchronize();
    }
}

template<typename Item>
Item* Queue<Item>::pop() {
    while(true) {
        QueueNode<Item>* headOld = this->DUMMY.next;
        if(headOld == 0) {
            __sync_synchronize();
            headOld = this->DUMMY.next;
            if(headOld == 0) return 0;
        }

        if(__sync_bool_compare_and_swap(&this->DUMMY.next, headOld, headOld->next) {
            Item* item = headOld->item;
            headOld->next = 0;
            delete headOld;
            headOld = 0;
            return item;
        }
    }
}

#endif /* INC_QUEUE_H_ */
