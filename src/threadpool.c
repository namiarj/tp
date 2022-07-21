#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "threadpool.h"

struct task_data {
	void (*run_tasks)(tpool_t, void *);
	void *arg;
};

struct tpool {
	struct task_data*	task_queue;
	int			queue_head, queue_tail;
	int			max_threads;
	int			active_threads;
	int			scheduled;
	pthread_mutex_t		mutex;
	pthread_t		*threads;
	pthread_cond_t 		task_available;
	pthread_cond_t 		done;
};

void
*run_tasks(void *arg)
{
	tpool_t pool = arg;
	struct task_data picked_task;
loop:
	pthread_mutex_lock(&pool->mutex);
	if (!pool->scheduled) {
		pool->active_threads--;
		if (!pool->active_threads)
			pthread_cond_signal(&pool->done);
		pthread_cond_wait(&pool->task_available, &pool->mutex);
		pool->active_threads++;
	} 
	picked_task = pool->task_queue[pool->queue_head++];
	pool->scheduled--;
	pthread_mutex_unlock(&pool->mutex);
	picked_task.run_tasks(pool,(void *)picked_task.arg);
	goto loop;
}

tpool_t
tpool_create(unsigned int num_threads)
{
	tpool_t pool = malloc(sizeof(struct tpool));
	pool->task_queue = malloc(sizeof(struct task_data));
	pool->queue_head = pool->queue_tail = pool->scheduled = 0;
	pool->active_threads = pool->max_threads = num_threads;
	pool->threads = malloc(sizeof(pthread_t) * num_threads);
	pthread_mutex_init(&pool->mutex, NULL);
	for (int i = 0; i < num_threads; i++)
		pthread_create(&pool->threads[i], NULL, &run_tasks, pool);
	return pool;
}

void
tpool_schedule_task(tpool_t pool, void (*fun)(tpool_t, void*), void *arg)
{
	struct task_data task;
	task.run_tasks = fun;
	task.arg = (void *)arg;
	pthread_mutex_lock(&pool->mutex);
	pool->task_queue[pool->queue_tail++] = task;
	pool->scheduled++;
	pool->task_queue = realloc(pool->task_queue, sizeof(struct task_data) * (pool->queue_tail + 1));
	pthread_mutex_unlock(&pool->mutex);
	pthread_cond_signal(&pool->task_available);
}

void
tpool_join(tpool_t pool)
{
	pthread_mutex_lock(&pool->mutex);
	while(pool->active_threads || pool->scheduled)
		pthread_cond_wait(&pool->done, &pool->mutex);
	for (int i = 0; i < pool->max_threads; i++)
		pthread_cancel(pool->threads[i]);
	free(pool->threads); 
	free(pool->task_queue); 
	free(pool); 
}
