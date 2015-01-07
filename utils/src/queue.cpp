/*
 * queue
 *
 *  Created on: Jan 7, 2015
 *      Author: felix
 */

template<typename Item>
QueueNode::QueueNode(item* i, const QueueNode* next) : item(item), next(next) {
}

template<typename Item>
QueueNode::~QueueNode() {
    this->next = NULL;
}

template<typename Item>
Queue::Queue() : head(NULL), tail(NULL) {
}

template<typename Item>
Queue::~Queue() {
    this->head = NULL;
    this->tail = NULL;
}

template<typename Item>
void Queue::push(Item* item) {
    QueueNode<Item>* tailNew = new QueueNode<Item>(item, NULL);
    while(true) {
        QueueNode<Item>* tailOld = this->tail;
        if(!tailOld->next) {
            if(__sync_bool_compare_and_swap(&tailOld->next, NULL, tailNew)) {
                __sync_bool_compare_and_swap(&this->tail, tailOld, tailNew);
                return;
            }
        }
        __sync_synchronize();
    }
}

template<typename Item>
Item* Queue::pop() {
    while(true) {
        QueueNode<Item>* headOld = this->head;
        if(headOld == NULL) {
            __sync_synchronize();
            headOld = this->head;
            if(headOld == NULL) return NULL;
        }
        
        if(__sync_bool_compare_and_swap(&this->head, headOld, headOld->next) {
            Item* item = headOld->item;
            delete headOld;
            return item; 
        }
    }
}



