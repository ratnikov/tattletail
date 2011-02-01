#include <stdlib.h>
#include <pthread.h>

#include "tsqueue.h"

int tsqueue_init(tsqueue_t * queue)
{
	int ret;

	queue->head = NULL;
	queue->last = NULL;

	if ((ret = pthread_mutex_init(&queue->mutex, NULL)) != 0)
	{
		return ret;
	}

	if ((ret = pthread_cond_init(&queue->signal, NULL)) != 0)
	{
		pthread_mutex_destroy(&queue->mutex);
		return ret;
	}

	return 0;
}

void tsqueue_enqueue(tsqueue_t * queue, void * item)
{
	tsqueue_node_t * node = malloc(sizeof(tsqueue_node_t));

	node->item = item;
	node->next = NULL;

	pthread_mutex_lock(&queue->mutex);

	if (queue->head == NULL)
	{
		queue->last = queue->head = node;
	}
	else
	{
		queue->last = queue->last->next = node;
	}

	pthread_cond_signal(&queue->signal);
	pthread_mutex_unlock(&queue->mutex);
}

void * tsqueue_dequeue(tsqueue_t * queue)
{
	tsqueue_node_t * node = NULL;
	void * item = NULL;

	pthread_mutex_lock(&queue->mutex);

	if (queue->head != NULL)
	{
		node = queue->head;
		queue->head = node->next;
	}

	pthread_mutex_unlock(&queue->mutex);

	if (node != NULL)
	{
		item = node->item;
		tsqueue_free_node(node);
	}

	return item;
}

void tsqueue_attach(tsqueue_t * queue, tsqueue_node_t * node)
{
	tsqueue_node_t * last_attached;

	pthread_mutex_lock(&queue->mutex);

	for (last_attached = node; last_attached && last_attached->next != NULL; last_attached = last_attached->next);

	if (queue->head == NULL)
	{
		queue->head = node;
		queue->last = last_attached;
	}
	else
	{
		queue->last->next = node;
		queue->last = last_attached;
	}

	pthread_cond_signal(&queue->signal);
	pthread_mutex_unlock(&queue->mutex);
}

tsqueue_node_t * tsqueue_detach(tsqueue_t * queue)
{
	tsqueue_node_t * detached = NULL;

	pthread_mutex_lock(&queue->mutex);

	if (queue->head != NULL)
	{
		detached = queue->head;
		queue->head = queue->last = NULL;
	}

	pthread_mutex_unlock(&queue->mutex);

	return detached;
}

void tsqueue_wait(tsqueue_t * queue)
{
	pthread_mutex_lock(&queue->mutex);

	if (queue->head == NULL)
	{
		pthread_cond_wait(&queue->signal, &queue->mutex);
	}

	pthread_mutex_unlock(&queue->mutex);
}

int tsqueue_free(tsqueue_t * queue)
{
	if (queue != NULL)
	{
		pthread_mutex_destroy(&queue->mutex);
		pthread_cond_destroy(&queue->signal);
		tsqueue_free_detached(queue->head);
	}
}

int tsqueue_free_node(tsqueue_node_t * node)
{
	free(node);
}

int tsqueue_free_detached(tsqueue_node_t * node)
{
	for(; node != NULL; node = node->next)
	{
		tsqueue_free_node(node);
	}
}
