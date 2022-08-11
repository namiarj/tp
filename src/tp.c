#include <err.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tp.h"

#define QUEUE_SIZE 128 
#define TASK_S_SIZE sizeof(struct task_s)

struct task_s {
    void (*tpool_run)(tpool_t, void *);
    void *arg;
};

struct tpool {
	struct task_s	*queue;
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
resize(tpool_t pool)
{
	struct task_s *resized = guard(malloc(TASK_S_SIZE * pool->size));
	pool->tail -= pool->head; 
	guard(memcpy(resized, &pool->queue[pool->head], TASK_S_SIZE * pool->tail));
	pool->head = 0;
	free(pool->queue);
	pool->queue = resized;
}

static void
*tpool_run(void *arg)
{
	tpool_t pool = arg;
	struct task_s task;

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

tpool_t
tpool_create(unsigned int num)
{
	tpool_t pool = guard(malloc(sizeof(struct tpool)));
	pool->queue = guard(malloc(TASK_S_SIZE * QUEUE_SIZE));
	pool->thread = guard(malloc(sizeof(pthread_t) * num));
	pool->size = QUEUE_SIZE;
	pool->active = pool->total = num;
	pool->head = pool->tail = pool->pending = 0;
	pool->shutdown = false;
	pthread_mutex_init(&pool->lock, NULL);

	for (; num > 0; num--)
		pthread_create(&pool->thread[num], NULL, &run_tasks, pool);

	return (pool);
}

void
tpool_task(tpool_t pool, void (*fun)(tpool_t, void*), void *arg)
{
	struct task_s task;
	task.run_tasks = fun;
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
tpool_join(tpool_t pool)
{
	pthread_mutex_lock(&pool->lock);

	while (pool->pending)
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
