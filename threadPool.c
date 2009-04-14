#include "router.h"
#include <string.h>

void initThreadPool(){
    int i = 0;
    pthread_mutex_init(&pool_lock, NULL);
    
	while(i < NUM_THREADS){    	
    	if (pthread_create(&workers[i], NULL, startThread, NULL) == 0)
    		i++;
    }    
}

void destroyThreadPool(){
	
	// add dummy node to queue and join threads
    pthread_mutex_destroy(&pool_lock);
}

void* startThread(void* dummy){
	return NULL;
}

void addThreadQueue(struct threadWorker** head, struct threadWorker** tail, struct sr_instance* sr, const uint8_t* packet, unsigned len, const char* interface){
	struct threadWorker* node = (struct threadWorker*)malloc(sizeof(struct threadWorker));
	
	node->sr = sr;
	node->packet = (uint8_t*)malloc(len*sizeof(uint8_t));
	memcpy(node->packet, packet, len);
	node->len = len;
	strcpy(node->interface, interface);
	node->stop_work = 0;
	node->prev = node->next = NULL;
	
	pthread_mutex_lock(&pool_lock);
	
	if(*head == NULL){
		*head = node;
		*tail = node;
	}
	else{
		(*head)->prev = node;
		node->next = *head;
	}	
		
	pthread_mutex_unlock(&pool_lock);

}

void addStopNode(struct threadWorker** head, struct threadWorker** tail){
	struct threadWorker* node = (struct threadWorker*)malloc(sizeof(struct threadWorker));
	
	node->sr = NULL;
	node->packet = NULL;
	node->len = 0;
	strcpy(node->interface, "");
	node->stop_work = 1;
	node->prev = node->next = NULL;
	
	pthread_mutex_lock(&pool_lock);
	
	if(*head == NULL){
		*head = node;
		*tail = node;
	}
	else{
		(*head)->prev = node;
		node->next = *head;
	}	
		
	pthread_mutex_unlock(&pool_lock);
}

struct threadWorker* takeThreadQueue(struct threadWorker** head, struct threadWorker** tail){
	struct threadWorker *retVal;
	pthread_mutex_lock(&pool_lock);
	if(*tail == NULL){
		retVal = NULL;
	}
	else{
		retVal = *tail;
		if((*tail)->prev){
			(*tail)->prev->next = NULL;
			*tail = (*tail)->prev;
		}
		else{
			*head = NULL;
			*tail = NULL;
		}
	}
	pthread_mutex_unlock(&pool_lock);

	return retVal;
}
