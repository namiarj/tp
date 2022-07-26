#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "threadpool.h"

#define DEFAULT_QUEUE_SIZE 64
#define TASK_S_SIZE sizeof(struct task_s)

struct task_s {
    void (*run_tasks)(tpool_t, void *);
    void *arg;
};

struct tpool {
	struct task_s	*task_queue;
	unsigned int	queue_size, queue_head, queue_tail;
	unsigned int	num_threads, active_threads, pending;
	bool		keepalive;
	pthread_mutex_t	mutex;
	pthread_t	*threads;
	pthread_cond_t 	notify, done;
};

static void
*run_tasks(void *arg)
{
	tpool_t pool = arg;
	struct task_s task;
loop:
	pthread_mutex_lock(&pool->mutex);
	while (!pool->pending && pool->keepalive) {
		if (!pool->--active_threads)
			pthread_cond_signal(&pool->done);
		pthread_cond_wait(&pool->notify, &pool->mutex);
		pool->active_threads++;
	}
	if (!pool->keepalive)
		goto shutdown;
	task = pool->task_queue[pool->queue_head++];
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
	tpool_t pool = malloc(sizeof(struct tpool));
	pool->queue_size = DEFAULT_QUEUE_SIZE;
	pool->task_queue = malloc(TASK_S_SIZE * pool->queue_size);
	pool->queue_head = pool->queue_tail = pool->pending = 0;
	pool->active_threads = pool->num_threads = num_threads;
	pool->threads = malloc(sizeof(pthread_t) * num_threads);
	pool->keepalive = true;
	pthread_mutex_init(&pool->mutex, NULL);
	for (int i = 0; i < num_threads; i++)
		pthread_create(&pool->threads[i], NULL, &run_tasks, pool);
	return (pool);
}

static void
resize_queue(tpool_t pool)
{
	struct task_s *resized_queue = malloc(TASK_S_SIZE * pool->queue_size);
	pool->queue_tail -= pool->queue_head; 
	memcpy(resized_queue, &pool->task_queue[pool->queue_head], TASK_S_SIZE * pool->queue_tail);
	pool->queue_head = 0;
	free(pool->task_queue);
	pool->task_queue = resized_queue;
}

void
tpool_schedule_task(tpool_t pool, void (*fun)(tpool_t, void*), void *arg)
{
	struct task_s task;
	task.run_tasks = fun;
	task.arg = (void *)arg;
	pthread_mutex_lock(&pool->mutex);
	pool->task_queue[pool->queue_tail++] = task;
	pool->pending++;
	if (pool->queue_tail == pool->queue_size) {
		pool->queue_size *= 2;
		resize_queue(pool);
	} else if (pool->queue_head > pool->queue_size / 2)
		resize_queue(pool);
	pthread_mutex_unlock(&pool->mutex);
	pthread_cond_signal(&pool->notify);
}

void
tpool_join(tpool_t pool)
{
	pthread_mutex_lock(&pool->mutex);
	while (pool->active_threads || pool->pending)
		pthread_cond_wait(&pool->done, &pool->mutex);
	pool->keepalive = false;
	pthread_cond_broadcast(&pool->notify);
	pthread_mutex_unlock(&pool->mutex);
	for (unsigned int i = 0; i < pool->num_threads; i++)
		pthread_join(pool->threads[i], NULL);
	free(pool->threads); 
	free(pool->task_queue); 
	free(pool); 
}
