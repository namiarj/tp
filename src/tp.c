#include <err.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tp.h"

#define QUEUE_SIZE 64 

struct task {
    void (*tpool_run)(tpool, void *);
    void *arg;
};

struct tpool {
	struct task	*queue;
	unsigned int	size, active, total, head, tail, pending;
	bool		shutdown;
	pthread_t	*thread;
	pthread_mutex_t	lock;
	pthread_cond_t 	notify, done;
};

static void
*guard(void *ptr)
{
	if (ptr == NULL)
		err(1, "returned null");

	return (ptr);
}

static void
resize(tpool pool)
{
	struct task *resized = guard(malloc(sizeof(struct task) * pool->size));
	pool->tail -= pool->head; 
	guard(memcpy(resized, &pool->queue[pool->head], sizeof(struct task) * pool->tail));
	pool->head = 0;
	free(pool->queue);
	pool->queue = resized;
}

static void
*tpool_run(void *arg)
{
	tpool pool = arg;
	struct task task;

loop:
	pthread_mutex_lock(&pool->lock);

	while (!pool->pending) {
		if (!--pool->active)
			pthread_cond_signal(&pool->done);
		pthread_cond_wait(&pool->notify, &pool->lock);
		pool->active++;
	}

	if (pool->shutdown) goto shutdown;

	pool->pending--;
	task = pool->queue[pool->head++];
	pthread_mutex_unlock(&pool->lock);
	task.tpool_run(pool, (void *)task.arg);
	goto loop;

shutdown:
	pthread_mutex_unlock(&pool->lock);
	pthread_exit(NULL);
	return (NULL);
}

tpool
tpool_create(unsigned int num)
{
	tpool pool = guard(malloc(sizeof(struct tpool)));
	pool->queue = guard(malloc(sizeof(struct task) * QUEUE_SIZE));
	pool->thread = guard(malloc(sizeof(pthread_t) * num));
	pool->size = QUEUE_SIZE;
	pool->active = pool->total = num;
	pool->head = pool->tail = pool->pending = 0;
	pool->shutdown = false;
	pthread_mutex_init(&pool->lock, NULL);

	for (; num > 0; num--)
		pthread_create(&pool->thread[num], NULL, &tpool_run, pool);

	return (pool);
}

void
tpool_task(tpool pool, void (*fun)(tpool, void*), void *arg)
{
	struct task task;
	task.tpool_run = fun;
	task.arg = (void *)arg;
	pthread_mutex_lock(&pool->lock);
	pool->queue[pool->tail++] = task;

	if (++pool->pending < pool->size / 4) {
		if (pool->size > QUEUE_SIZE)
			pool->size /= 2;
		resize(pool);
	} else if (pool->tail == pool->size) {
		pool->size *= 2;
		resize(pool);
	}

	pthread_mutex_unlock(&pool->lock);
	pthread_cond_signal(&pool->notify);
}

void
tpool_join(tpool pool)
{
	pthread_mutex_lock(&pool->lock);
	pthread_cond_wait(&pool->done, &pool->lock);
	pool->shutdown = true;
	pthread_cond_broadcast(&pool->notify);
	pthread_mutex_unlock(&pool->lock);

	for (; pool->total > 0; pool->total--)
		pthread_join(pool->thread[pool->total], NULL);

	pthread_mutex_destroy(&pool->lock);
	pthread_cond_destroy(&pool->notify);
	pthread_cond_destroy(&pool->done);
	free(pool->thread); 
	free(pool->queue); 
	free(pool);
}
