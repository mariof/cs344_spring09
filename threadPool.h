#include <pthread.h>
#include "sr_base_internal.h"

#define NUM_THREADS 1

pthread_t workers[NUM_THREADS];

struct threadWorker{
	uint8_t* packet;
	unsigned len;
	char interface[SR_NAMELEN];
	int stop_work;
	struct threadWorker* prev;
	struct threadWorker* next;	
};

pthread_mutex_t pool_lock;

void initThreadPool();
void destroyThreadPool();
void* startThread(void* dummy);
void addStopNode(struct threadWorker** head, struct threadWorker** tail);
void addThreadQueue(struct sr_instance* sr, const uint8_t* packet, unsigned len, const char* interface);
struct threadWorker* takeThreadQueue(struct threadWorker** head, struct threadWorker** tail);
