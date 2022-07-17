#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "threadpool.h"

struct task_data {
	void (*run_tasks)(tpool_t, void *);
	void *arg;
};

struct tpool {
	struct task_data	*task_queue;
	int			queue_head, queue_tail;
	int			scheduled;
	int			active_workers;
	int			max_workers;
	pthread_t		*workers;
	pthread_mutex_t		mutex;
	pthread_cond_t 		work_available;
	pthread_cond_t 		done;
};

void
*run_tasks(void *param)
{
	tpool_t pool = param;
	struct task_data picked_task;
loop:
	pthread_mutex_lock(&pool->mutex);
	if (!pool->scheduled) {
	pool->active_workers--;
	if (!pool->active_workers)
		pthread_cond_signal(&pool->done);
	pthread_cond_wait(&pool->work_available, &pool->mutex);
	pool->active_workers++;
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
	tpool_t pool = malloc(sizeof(tpool_t));
	pool->task_queue = malloc(sizeof(struct task_data));
	pool->queue_head = pool->queue_tail = pool->scheduled = 0;
	pool->active_workers = pool->max_workers = num_threads;
	pool->workers = malloc(sizeof(pthread_t) * num_threads);
	pthread_mutex_lock(&pool->mutex);
	for (int i = 0; i < num_threads; i++)
		pthread_create(&pool->workers[i], NULL, &run_tasks, pool);
	return pool;
}

void
tpool_schedule_task(tpool_t pool, void (*fun)(tpool_t, void*), void *arg)
{
	struct task_data task;
	pthread_mutex_lock(&pool->mutex);
	task.run_tasks = fun;
	task.arg = (void *)arg;
	pool->task_queue[pool->queue_tail++] = task;
	pool->scheduled++;
	pool->task_queue = realloc(pool->task_queue, sizeof(struct task_data) * (pool->queue_tail + 1));
	pthread_cond_signal(&pool->work_available);
	pthread_mutex_unlock(&pool->mutex);
}

void
tpool_join(tpool_t pool)
{
	pthread_mutex_lock(&pool->mutex);
	while(pool->active_workers || pool->scheduled)
		pthread_cond_wait(&pool->done, &pool->mutex);
	free(pool->task_queue); 
	for (int i = 0; i < pool->max_workers; i++)
		pthread_cancel(pool->workers[i]);
}
