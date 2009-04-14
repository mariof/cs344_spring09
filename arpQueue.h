#include "sr_vns.h"
#include "sr_base_internal.h"
#include "sr_integration.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>

#define ARP_QUEUE_TIMEOUT 4

struct arpQueueItem{
	time_t t;
	uint8_t* packet;
	unsigned len;
	struct arpQueueItem *next;
	struct arpQueueItem *prev;
};

struct arpQueueNode{
	uint32_t dstIP;
	char interface[SR_NAMELEN];
	struct arpQueueItem* head;
	struct arpQueueItem* tail;
	struct arpQueueNode* next;
	struct arpQueueNode* prev;
};

void queuePacket(uint8_t* packet, unsigned len, const char* interface, uint32_t dstIP);
void queueSend(uint32_t ip, const char* interface);

void* arpQueueRefresh(void* dummy);

pthread_mutex_t queue_lock;
