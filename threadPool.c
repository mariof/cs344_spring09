#include "router.h"
#include <string.h>
#include "lwtcp/lwip/sys.h"

// initializes thread pool
void initThreadPool(){
    int i = 0;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	subsystem->poolHead = subsystem->poolTail = NULL;

    pthread_mutex_init(&pool_lock, NULL);
    
	while(i < NUM_THREADS){    	
    	sys_thread_new(startThread, NULL);
    	i++;
    	//if (pthread_create(&workers[i], NULL, startThread, NULL) == 0){
    	//	i++;
    	//}
    }    
    dbgMsg("Thread Pool Initialized");
}


// destroys thread pool (joins all spawned threads)
void destroyThreadPool(){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	for(i = 0; i < NUM_THREADS; i++){
		addStopNode(&subsystem->poolHead, &subsystem->poolTail);
	}
//	for(i = 0; i < NUM_THREADS; i++){
//		pthread_join(workers[i], NULL);
//	}

	pthread_mutex_lock(&pool_lock);
	struct threadWorker* cur = subsystem->poolHead;
	struct threadWorker* tmp;
	while(cur){
		if(cur->packet) free(cur->packet);
		tmp = cur;
		cur = cur->next;
		free(tmp);
	}
	pthread_mutex_unlock(&pool_lock);

    pthread_mutex_destroy(&pool_lock);
    dbgMsg("Tread Pool destroyed");
}

// main thread function
void startThread(void* dummy){
	struct threadWorker *w;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	
	while(1){
		w = takeThreadQueue(&subsystem->poolHead, &subsystem->poolTail);
		if(w){
			dbgMsg("Job taken off queue");
			if(w->stop_work){
				if(w->packet) free(w->packet);
				free(w);
				return;			
			}
			else{
				processPacket(sr, w->packet, w->len, w->interface);				
				if(w->packet) free(w->packet);
				free(w);
			}
		}
		else{
			usleep(10);				
		}
	}
	return;
}

// adds a job to the queue (a packet to process)
void addThreadQueue(struct sr_instance* sr, const uint8_t* packet, unsigned len, const char* interface){
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	
	struct threadWorker* node = (struct threadWorker*)malloc(sizeof(struct threadWorker));
	
	node->packet = (uint8_t*)malloc(len*sizeof(uint8_t));
	memcpy(node->packet, packet, len);
	node->len = len;
	strcpy(node->interface, interface);
	node->stop_work = 0;
	node->prev = node->next = NULL;
	
	pthread_mutex_lock(&pool_lock);
	struct threadWorker** head = &subsystem->poolHead;
	struct threadWorker** tail = &subsystem->poolTail;
	
	if(*head == NULL){
		*head = node;
		*tail = node;
	}
	else{
		(*head)->prev = node;
		node->next = *head;
		*head = node;
	}	
	pthread_mutex_unlock(&pool_lock);
	dbgMsg("Job put in queue");
}

// adds a stop node to the queue (this node causes all spawned threads to exit)
void addStopNode(struct threadWorker** head, struct threadWorker** tail){
	struct threadWorker* node = (struct threadWorker*)malloc(sizeof(struct threadWorker));

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
		*head = node;
	}	
		
	pthread_mutex_unlock(&pool_lock);
}

// takes next packet in the queue for processing, whoever calls this gets the ownership of the returned node
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
