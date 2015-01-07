/*
 * queue.h
 *
 *  Created on: Jan 7, 2015
 *      Author: felix
 */

#ifndef QUEUE_H_
#define QUEUE_H_

template<typename Item>
class QueueNode {
private:
    Item* item;
    const QueueNode* next;
    
public:
    QueueNode(Item* item, const QueueNode* next);
    virtual ~QueueNode();
};

template<typename Item>
class Queue {
private:
	const QueueNode<Item>* head;
	const QueueNode<Item>* tail;
	
public:
    Queue();
    virtual ~Queue();
    
    void push(Item* item);
    Item* pop();
};

#include "queue.cpp"

#endif /* QUEUE_H_ */
