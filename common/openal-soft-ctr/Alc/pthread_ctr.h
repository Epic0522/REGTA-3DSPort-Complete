#include <3ds.h>

#ifndef PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_RECURSIVE 0
#endif
#ifndef PTHREAD_ONCE_INIT
#define PTHREAD_ONCE_INIT {0}
#endif

/* its probably better to just wrap the few pthread routines
   than to insert a bunch of ifdefs */

/* typedef Thread	pthread_t; */
/* typedef u32	pthread_key_t; */
/* typedef u32	pthread_once_t; */
/* typedef u32	pthread_attr_t; */
/* typedef u32	pthread_mutexattr_t; */
/* typedef Handle	pthread_mutex_t; */
/* typedef u32	pthread_once_t; */

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine) (void *), void *arg);

int pthread_join(pthread_t thread, void **retval);
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

int pthread_mutexattr_init(pthread_mutexattr_t *attrib);
int pthread_mutexattr_destroy(pthread_mutexattr_t *mutex);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);

int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_once(pthread_once_t *once, void (*callback)(void));

int pthread_key_create(pthread_key_t *key, void (*callback)(void*));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *val);
int sched_yield(void);
