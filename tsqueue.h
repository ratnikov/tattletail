#ifndef TSQUEUE_H_JCVJX1OD
#define TSQUEUE_H_JCVJX1OD

#include <pthread.h>

typedef struct tsqueue_node {
	void * item;
	struct tsqueue_node * next;
} tsqueue_node_t;

typedef struct tsqueue {
	struct tsqueue_node * head, * last;
	pthread_mutex_t mutex;
	pthread_cond_t signal;
} tsqueue_t;

int tsqueue_init(tsqueue_t *);
void tsqueue_enqueue(tsqueue_t *, void *);
void * tsqueue_dequeue(tsqueue_t *);
void tsqueue_attach(tsqueue_t *, tsqueue_node_t *);
tsqueue_node_t * tsqueue_detach(tsqueue_t *);
void tsqueue_wait(tsqueue_t *);
int tsqueue_free(tsqueue_t *);
int tsqueue_free_node(tsqueue_node_t *);
int tsqueue_free_detached(tsqueue_node_t *);

#endif /* end of include guard: TSQUEUE_H_JCVJX1OD */
