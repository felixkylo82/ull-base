# ull-base

Very often, when a system receives a request, the request is queued before it is processed. However, queuing imposes significant latency. In low latency systems, the queue performance becomes critical. To avoid buffer overflow and to minimize message loss, messages have to be queued immediately after arrival. Meanwhile, latency imposed should be as little as possible.

Taking market data dispatches as an example, moldudp64 is a udp protocol employed by NASDAQ exchange. Under this protocol, messages contain sequence numbers so that when they are lost, they can be retransmitted via a certain recovery mechanism on requests. However, retransmission may impose latency more than 10ms.

To address this problem, ull-base assumes a single thread enqueues(/produces) messages and a single thread dequeue(/consumes) messages. The order of memory allocation of messages is always the same as the order of memory deallocation of messages. In other words, a thread allocates a buffer, receives messages from the network and enqueues the messages. Another thread dequeues the meesages, processes the messages and then deallocates the messages.

ull-base consists of two C++ template classes, Memory and Queue.

Memory is a list of memory nodes and each node has memory blocks where the overall size is the multiples of the size of a cache line. During allocation, to ensure proper memory alignments, memory segment will be enlarged a little bit so that its size become the multiples of 8 bytes. When the memory node is full, a new node is created. On the other hand, when the memory node is free, the node is kept in another list.

Queue is also a list of nodes and each node has an array of pointers to the data segment. The same mechanism for the insertion and the reservation of nodes applies.

Experimental results, performed on an intel i3 machine, show that, at data rate = 1,000,000 messages per second, only 1/1,000,000 messages to which the imposed latency is >= 100us. 
