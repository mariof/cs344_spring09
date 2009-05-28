#ifndef GW_LIST_H
#define GW_LIST_H

#include "sr_vns.h"
#include "sr_base_internal.h"
#include "sr_integration.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>

struct gwListNode{
	uint32_t gw;
	struct gwListNode *next;
};

pthread_mutex_t gw_lock;

int gwList_insert(struct gwListNode **head, uint32_t gw);
void gwList_flush(struct gwListNode **head);


#endif // GW_LIST_H
