/* libretro's thread calls used by parallel-psx's texture tracker (its file I/O thread), on the C++
 * standard library. Written for this port. */
#pragma once
#include <condition_variable>
#include <mutex>
#include <thread>

typedef std::mutex slock_t;
typedef std::condition_variable_any scond_t;
typedef std::thread sthread_t;

static inline slock_t *slock_new(void) { return new std::mutex; }
static inline void slock_free(slock_t *l) { delete l; }
static inline void slock_lock(slock_t *l) { l->lock(); }
static inline void slock_unlock(slock_t *l) { l->unlock(); }
static inline scond_t *scond_new(void) { return new std::condition_variable_any; }
static inline void scond_free(scond_t *c) { delete c; }
static inline void scond_wait(scond_t *c, slock_t *l) { c->wait(*l); }
static inline void scond_signal(scond_t *c) { c->notify_one(); }
static inline sthread_t *sthread_create(void (*fn)(void *), void *arg) { return new std::thread(fn, arg); }
static inline void sthread_detach(sthread_t *t) { t->detach(); delete t; }
