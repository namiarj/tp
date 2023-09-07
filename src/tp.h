#pragma once

typedef struct tpool *tpool;
static void *guard(void *ptr);
static void resize(tpool pool);
tpool tpool_create(unsigned int num);
void tpool_task(tpool pool, void (*fun)(tpool, void *), void *arg);
void tpool_join(tpool pool);
