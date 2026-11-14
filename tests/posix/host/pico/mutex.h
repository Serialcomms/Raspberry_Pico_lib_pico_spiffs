/* pico/mutex.h - host stand-in: a recursive mutex that checks balance. */
#ifndef HOST_PICO_MUTEX_H
#define HOST_PICO_MUTEX_H
#include <assert.h>
#include "pico.h"
typedef struct { int depth; } recursive_mutex_t;
#define auto_init_recursive_mutex(name) recursive_mutex_t name
static inline void recursive_mutex_enter_blocking(recursive_mutex_t *m) { m->depth++; }
static inline void recursive_mutex_exit(recursive_mutex_t *m) { assert(m->depth > 0); m->depth--; }
#endif
