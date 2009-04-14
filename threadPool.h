#include <pthread.h>
#include "sr_base_internal.h"

#define NUM_THREADS 5

pthread_t workers[NUM_THREADS];

struct threadWorker{
	struct sr_instance* sr;
	uint8_t* packet;
	unsigned len;
	char interface[SR_NAMELEN];
	int stop_work;
	struct threadWorker* prev;
	struct threadWorker* next;	
};

pthread_mutex_t pool_lock;


void* startThread(void* dummy);
