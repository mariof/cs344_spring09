#include "router.h"
#include <string.h>

// caller must hold queue lock
struct arpQueueNode* addQueueNode(uint32_t ip, const char* interface){
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	struct arpQueueNode *cur = subsystem->arpQueue;
	while(cur){
		if( !strcmp(interface, cur->interface) && (ip == cur->dstIP) ){
			return cur;
		}
		cur = cur->next;
	}
	
	cur = (struct arpQueueNode*)malloc(sizeof(struct arpQueueNode));
	cur->interface = interface;
	cur->dstIP = ip;
	cur->head = cur->tail = NULL;
	cur->prev = NULL;
	cur->next = subsystem->arpQueue;
	if(subsystem->arpQueue) subsystem->arpQueue->prev = cur;
	subsystem->arpQueue = cur;
	
	return cur;
}

// add packet to queue, packet is borrowed
void queuePacket(uint8_t* packet, unsigned len, const char* interface, uint32_t dstIP){
	int i;
	pthread_mutex_lock(&queue_lock);

	struct arpQueueNode *node = addQueueNode(dstIP, interface);
	
	// create new item
	struct arpQueueItem *item = (struct arpQueueItem*)malloc(sizeof(struct arpQueueItem));
	item->packet = (uint8_t*)malloc(len * sizeof(uint8_t));
	for(i = 0; i < len; i++) item->packet[i] = packet[i];
	item->len = len;
	item->t = time(NULL);
	item->prev = item->next = NULL;
	
	// add item to node
	item->next = node->head;
	if(node->head) node->head->prev = item;
	node->head = item;
	if(node->tail == NULL) node->tail = item;
	pthread_mutex_unlock(&queue_lock);	

}

// flush a particular ip queue, caller must hold queue lock
void queueSendLockless(uint32_t ip, const char* interface){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// get destination MAC
	uint8_t *dstMAC = arpLookupTree(subsystem->arpTree, ip);

	if(dstMAC){
		struct arpQueueNode* cur = subsystem->arpQueue;
		while(cur){
			if( !strcmp(interface, cur->interface) && (ip == cur->dstIP) ){
				while( cur->tail ){
					for (i = 0; i < 6; i++) cur->tail->packet[i] = dstMAC[i];
					sr_integ_low_level_output(sr, cur->tail->packet, cur->tail->len, cur->interface);
					struct arpQueueItem* tmp = cur->tail;
					if(cur->tail->prev) 
						cur->tail->prev->next = NULL;
					else
						cur->head = NULL;
					cur->tail = cur->tail->prev;
					free(tmp);
				}
				struct arpQueueNode* curTmp = cur;
				cur = cur->next;	
				if(curTmp->prev) 
					curTmp->prev->next = curTmp->next;
				else
					subsystem->arpQueue = curTmp->next;
				if(curTmp->next)
					curTmp->next->prev = curTmp->prev;
				free(curTmp);
				break;
			}
			else{
				cur = cur->next;
			}
		}
		free(dstMAC);
	}

}

// flush a particular ip queue
void queueSend(uint32_t ip, const char* interface){
	pthread_mutex_lock(&queue_lock);
	queueSendLockless(ip, interface);
	pthread_mutex_unlock(&queue_lock);
}

// refresh all arp queues (timeout if neccessary)
void* arpQueueRefresh(void* dummy){
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	while(1){
		loop_begin:
		pthread_mutex_lock(&queue_lock);
		struct arpQueueNode* cur = subsystem->arpQueue;
		while(cur){
			struct arpQueueNode* tmp = cur->next;
			int delFlag = 0;
			while(cur->tail){
				if( (time(NULL) - cur->tail->t) > ARP_QUEUE_TIMEOUT){
					struct arpQueueItem* curTmp = cur->tail;
					if(curTmp->prev){ 
						curTmp->prev->next = NULL;
					}
					else{
						cur->head = NULL;
						delFlag = 1;
					}
					cur->tail = cur->tail->prev;
					// send out ICMP (host unreachable)
					pthread_mutex_unlock(&queue_lock);
					sendICMPDestinationUnreachable(cur->interface, curTmp->packet, curTmp->len, 1);
					free(curTmp);			
					goto loop_begin; // no way anoyone is going to convince me that there is a better way to to this (mariof)
				}
				else{
					break;
				}
			}
			if(delFlag){
				struct arpQueueNode* curTmpN = cur;
				if(curTmpN->prev) 
					curTmpN->prev->next = curTmpN->next;
				else
					subsystem->arpQueue = curTmpN->next;
				if(curTmpN->next)
					curTmpN->next->prev = curTmpN->prev;
				free(curTmpN);			
			}
			else{
				queueSendLockless(cur->dstIP, cur->interface);	
			}
			
			cur = tmp;
		}
		pthread_mutex_unlock(&queue_lock);
		sleep(ARP_QUEUE_REFRESH);
	}	
}
