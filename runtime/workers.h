/* Worker threads (workers.c): one lock and two conditions to wait on while holding it. */
#pragma once

#define WK_WORK 0       /* the band workers wait for drawing */
#define WK_DONE 1       /* the GPU waits for the band workers to finish */

int  wk_thread(int (*fn)(void *), void *arg);   /* 1 = started */
void wk_lock(void);
void wk_unlock(void);
void wk_wait(int cond);                         /* with the lock held */
void wk_wake(int cond);                         /* wakes every waiter */
int  wk_cpu_count(void);                        /* logical processors */
