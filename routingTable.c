#include "routingTable.h"

void insert_rtable_node(rtableNode **head, uint32_t ip, uint8_t netmask, const char* output_if)
{
    //check output_if size
    if(strlen(output_if) >= SR_NAMELEN) {
	return;
    }

    //create new node
    rtableNode *node = (rtableNode*) malloc(sizeof(rtableNode));
    node->ip = ip;
    node->netmask = netmask;
    strcpy(node->output_if, output_if);
    node->next = node->prev = NULL;

    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    //Check if the list is empty
    if(*head == NULL) {
	(*head) = node;
	pthread_mutex_unlock(&rtable_lock);
	return;
    }

    //scan the list until you hit the right netmask
    rtableNode *cnode = *head;
    if(netmask > cnode->netmask || (netmask == cnode->netmask && ip > cnode->ip)) {
	*head = node;
	node->next = cnode;
	cnode->prev = node;
    }
    else {
	while(cnode->next != NULL) {
	    if(netmask > cnode->next->netmask || (netmask == cnode->next->netmask && ip > cnode->next->ip)) {
		break;
	    }
	    cnode = cnode->next;
	}

	//check for equality to prevent adding duplicate nodes
	if((cnode->ip == ip) && ((uint8_t)cnode->netmask == (uint8_t)netmask)) {
	    strcpy(cnode->output_if, output_if);
	    free(node);
	    pthread_mutex_unlock(&rtable_lock);
	    return;
	}

	// insert new node
	if(cnode->next != NULL) {
	    (cnode->next)->prev = node;
	}
	node->next = cnode->next;
	node->prev = cnode;
	cnode->next = node;
    }

    //release lock
    pthread_mutex_unlock(&rtable_lock);
    return;
}

void del_ip(rtableNode **head, uint32_t ip, uint8_t netmask)
{
    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    // search node
    rtableNode *node = *head;
    while(node != NULL) {
	if(node->ip == ip && node->netmask == netmask) {
	    //delete node
	    if(node->prev != NULL) {
		(node->prev)->next = node->next;
	    }
	    if(node->next != NULL) {
		(node->next)->prev = node->prev;
	    }
	    free(node);
	}
    }
    //release lock
    pthread_mutex_unlock(&rtable_lock);
}

/* Return value: 
 * pointer to the interface name if lp match found
 * NULL pointer otherwise
 *
 * Note: the function allocates memory to store the return value
 * The caller must deallocate it after the function returns
 */

char *lp_match(rtableNode **head, uint32_t ip)
{
    char *output_if = NULL;

    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    //do LP matching
    rtableNode *node = *head;
    while(node != NULL) {
	if((node->ip >> (32-node->netmask)) == (ip >> (32-node->netmask))) {
	    //malloc 32 bytes for storing interface
	    output_if = (char*)malloc((sizeof(char)) * SR_NAMELEN);
	    strcpy(output_if, node->output_if);
	}
    }
    //release lock
    pthread_mutex_unlock(&rtable_lock);

    //return pointer to the interface buffer
    return output_if;
}

