#include "pthread_ctr.h"

/* haha probably the worst pthread "implementation" ever. */

#define S64_MAX 0x7fffffffffffffff

#define MAX_KEYS 32
static int pthread_key_index = 0;

typedef void (*dconfn)(void*);

dconfn         pthread_dcon[MAX_KEYS]; /* global */
__thread void* pthread_keys[MAX_KEYS]; /* thread-local */

int
sched_yield(void)
{
	svcSleepThread(1000000); /* one millisecond */
	return 0;
}

/* void */
/* threadWrapper(void (*fn)(void*), void *arg) */
/* { */
/* 	int i; */
/* 	void *rv = fn(); */

/* 	for(i = 0; i < MAX_KEYS; i++){ */
/* 		if (pthread_dcon[i]){ */
/* 			pthread_dcon(pthread_keys[i]); */
/* 		} */
/* 	} */
	
/* 	threadExit((int)fv); */
/* } */

int
pthread_create(pthread_t *thread, const pthread_attr_t *attr,
	       void *(*start_routine) (void *), void *arg)
{
	*thread	= (pthread_t)
		threadCreate((ThreadFunc)start_routine,
			     arg,
			     1024*128,
			     0x30,
			     -1,
			     false);
	return 0;
}

int
pthread_join(pthread_t thread, void **retval)
{
	threadJoin((Thread)thread, U64_MAX);
	return 0;
}

int
pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
#if __NEWLIB__ >= 4
	attr->type = PTHREAD_MUTEX_NORMAL;
#endif
	return 0;
}

int
pthread_mutexattr_settype(pthread_mutexattr_t *attr, int flags)
{
#if __NEWLIB__ >= 4
	attr->type = flags;
#endif
	return 0;
}

int
pthread_mutexattr_destroy(pthread_mutexattr_t *mutex)
{
	return 0;
}

int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
#if __NEWLIB__ >= 4
	mutex->type = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
	if(mutex->type == PTHREAD_MUTEX_RECURSIVE)
		RecursiveLock_Init(&mutex->recursive);
	else
		LightLock_Init(&mutex->normal);
#else
	svcCreateMutex(mutex, false);
#endif
	return 0;
}

int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
#if __NEWLIB__ >= 4
	if(mutex->type == PTHREAD_MUTEX_RECURSIVE)
		RecursiveLock_Lock(&mutex->recursive);
	else
		LightLock_Lock(&mutex->normal);
#else
	svcWaitSynchronization(mutex, S64_MAX);
#endif
	return 0;
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
#if __NEWLIB__ >= 4
	if(mutex->type == PTHREAD_MUTEX_RECURSIVE)
		RecursiveLock_Unlock(&mutex->recursive);
	else
		LightLock_Unlock(&mutex->normal);
#else
	svcReleaseMutex(mutex);
#endif
	return 0;
}

int
pthread_once(pthread_once_t *once, void (*callback)(void))
{
#if __NEWLIB__ >= 4
	int expected = 0;
	if(__atomic_compare_exchange_n(&once->status, &expected, 1, false,
	                               __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)){
		callback();
		__atomic_store_n(&once->status, 2, __ATOMIC_RELEASE);
	}else{
		while(__atomic_load_n(&once->status, __ATOMIC_ACQUIRE) != 2)
			svcSleepThread(1000000);
	}
#else
	if(!AtomicPostIncrement(&once->is_initialized)){
		callback();
		__dsb();
		once->init_executed = 1;
	}else {
		while (!once->init_executed){
			svcSleepThread(1000000);
		}
	}
#endif
	return 0;
}

int
pthread_key_create(pthread_key_t *key, void (*destructor)(void*))
{
	*key = AtomicPostIncrement(&pthread_key_index);

	if(*key > MAX_KEYS){
		return 1; /* ENOMEM */
	}
	
	pthread_dcon[*key] = destructor;

	return 0;
}

int
pthread_key_delete(pthread_key_t key)
{
	return 0;
}

void*
pthread_getspecific(pthread_key_t key)
{
	return pthread_keys[key];
}

int
pthread_setspecific(pthread_key_t key, const void *val)
{
	pthread_keys[key] = (void*)val;
	return 0;
}
