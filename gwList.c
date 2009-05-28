#include "router.h"

// Inserts gateway into the list if it is not in it already and returns its index in the list
// In case of an error returns -1
int gwList_insert(struct gwListNode **head, uint32_t gw){
	int i = 0;
	
	pthread_mutex_lock(&gw_lock);
	struct gwListNode *node = *head;
	struct gwListNode *prev = NULL;
	while(node){
		if(gw == node->gw) break;
		i++;	
		prev = node;
		node = node->next;
	}
	if(node == NULL){
		node = (struct gwListNode*)malloc(sizeof(struct gwListNode));
		if(node){
			node->gw = gw;
			node->next = NULL;
			if(prev)
				prev->next = node;
			else
				*head = node;			
		}
		else{
			i = -1;
		}
	}
	pthread_mutex_unlock(&gw_lock);

	// if i bigger than 255 something is wrong
	if(i > 0xff) i = -1;
	
	return i;
}

// Empties given gwList
void gwList_flush(struct gwListNode **head){
	pthread_mutex_lock(&gw_lock);
	struct gwListNode *node = *head;
	struct gwListNode *tmp;
	while(node){
		tmp = node;
		node = node->next;
		free(tmp);
	}
	*head = NULL;
	pthread_mutex_unlock(&gw_lock);
}

