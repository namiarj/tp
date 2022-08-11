#pragma once

typedef struct tpool *tpool_t;
static void guard(void *ptr);
static void resize(tpool_t pool);
tpool_t tpool_create(unsigned int num);
void tpool_task(tpool_t pool, void (*fun)(tpool_t, void *), void *arg);
void tpool_join(tpool_t pool);
