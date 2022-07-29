#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "threadpool.h"

#define QUEUE_SIZE 128 
#define TASK_S_SIZE sizeof(struct task_s)

struct task_s {
    void (*run_tasks)(tpool_t, void *);
    void *arg;
};

struct tpool {
	struct task_s	*queue;
	unsigned int	active, total, head, tail, pending, size;
	bool		keepalive;
	pthread_t	*threads;
	pthread_mutex_t	mutex;
	pthread_cond_t 	notify, done;
};

static void
*guard(char *fun, void *ptr)
{
	if (ptr == NULL) {
		printf("%s returned null", fun);
		exit(1);
	}
	return ptr;
}

static void
resize(tpool_t pool)
{
	struct task_s *resized = guard("malloc", malloc(TASK_S_SIZE * pool->size));
	pool->tail -= pool->head; 
	guard("memcpy", memcpy(resized, &pool->queue[pool->head], TASK_S_SIZE * pool->tail));
	pool->head = 0;
	free(pool->queue);
	pool->queue = resized;
}

static void
*run_tasks(void *arg)
{
	tpool_t pool = arg;
	struct task_s task;
loop:
	pthread_mutex_lock(&pool->mutex);
	while (!pool->pending && pool->keepalive) {
		if (!--pool->active)
			pthread_cond_signal(&pool->done);
		pthread_cond_wait(&pool->notify, &pool->mutex);
		pool->active++;
	}
	if (!pool->keepalive)
		goto shutdown;
	task = pool->queue[pool->head++];
	pool->pending--;
	pthread_mutex_unlock(&pool->mutex);
	task.run_tasks(pool,(void *)task.arg);
	goto loop;
shutdown:
	pthread_mutex_unlock(&pool->mutex);
	pthread_exit(NULL);
	return (NULL);
}

tpool_t
tpool_create(unsigned int num_threads)
{
	tpool_t pool = guard("malloc", malloc(sizeof(struct tpool)));
	pool->active = pool->total = num_threads;
	pool->head = pool->tail = pool->pending = 0;
	pool->size = QUEUE_SIZE;
	pool->queue = guard("malloc", malloc(TASK_S_SIZE * pool->size));
	pool->threads = guard("malloc", malloc(sizeof(pthread_t) * num_threads));
	pool->keepalive = true;
	pthread_mutex_init(&pool->mutex, NULL);
	for (int i = 0; i < num_threads; i++)
		pthread_create(&pool->threads[i], NULL, &run_tasks, pool);
	return (pool);
}

void
tpool_schedule_task(tpool_t pool, void (*fun)(tpool_t, void*), void *arg)
{
	struct task_s task;
	task.run_tasks = fun;
	task.arg = (void *)arg;
	pthread_mutex_lock(&pool->mutex);
	pool->queue[pool->tail++] = task;
	if (++pool->pending < pool->size / 4) {
		if (pool->size > QUEUE_SIZE)
			pool->size /= 2;
		resize(pool);
	} else if (pool->tail == pool->size) {
		pool->size *= 2;
		resize(pool);
	}
	pthread_mutex_unlock(&pool->mutex);
	pthread_cond_signal(&pool->notify);
}

void
tpool_join(tpool_t pool)
{
	pthread_mutex_lock(&pool->mutex);
	while (pool->active || pool->pending)
		pthread_cond_wait(&pool->done, &pool->mutex);
	pool->keepalive = false;
	pthread_cond_broadcast(&pool->notify);
	pthread_mutex_unlock(&pool->mutex);
	for (unsigned int i = 0; i < pool->total; i++)
		pthread_join(pool->threads[i], NULL);
	pthread_mutex_destroy(&pool->mutex);
	pthread_cond_destroy(&pool->notify);
	pthread_cond_destroy(&pool->done);
	free(pool->threads); 
	free(pool->queue); 
	free(pool);
}
