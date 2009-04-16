#include "router.h"
#include "routingTable.h"

void insert_rtable_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t gateway, const char* output_if, int is_static)
{
    //check output_if size
    if(strlen(output_if) >= SR_NAMELEN) {
	return;
    }

    //create new node
    rtableNode *node = (rtableNode*) malloc(sizeof(rtableNode));
    node->ip = ip;
    node->netmask = netmask;
    node->gateway = gateway;
    strcpy(node->output_if, output_if);
    node->is_static = is_static;
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
	if((cnode->ip == ip) && ((uint8_t)cnode->netmask == (uint8_t)netmask) && (cnode->is_static == is_static)) {
	    cnode->gateway = gateway;
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

int del_ip(rtableNode **head, uint32_t ip, uint8_t netmask, int is_static)
{
    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    // search node
    rtableNode *node = *head;
    while(node != NULL) {
	if(node->ip == ip && node->netmask == netmask && node->is_static == is_static) {
	    //delete node
	    if(node->prev != NULL) {
		(node->prev)->next = node->next;
	    }
	    if(node->next != NULL) {
		(node->next)->prev = node->prev;
	    }
	    free(node);
	    pthread_mutex_unlock(&rtable_lock);
	    return 1;
	}
	node = node->next;
    }
    //release lock
    pthread_mutex_unlock(&rtable_lock);
    return 0;
}

void del_route_type(rtableNode **head, int is_static)
{
    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    // search node
    rtableNode *node = *head;
    while(node != NULL) {
	if(node->is_static == is_static) {
	    //delete node
	    if(node->prev != NULL) {
		(node->prev)->next = node->next;
	    }
	    if(node->next != NULL) {
		(node->next)->prev = node->prev;
	    }
	    free(node);
	}
	node = node->next;
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
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    //do LP matching
    rtableNode *node = *head;
    while(node != NULL) {
	if((node->ip & node->netmask) == (ip & node->netmask)) {
	    //malloc 32 bytes for storing interface
	    output_if = (char*)malloc((sizeof(char)) * SR_NAMELEN);
	    int i;
	    int iface_disabled = 0;
	    for(i = 0; i < subsystem->num_ifaces; i++) {
		if(!strcmp(subsystem->ifaces[i].name, output_if)) {
		    if(!(subsystem->ifaces[i].enabled)) {
			iface_disabled = 1;
		    }
		    break;
		}
	    }

	    if(iface_disabled)
		continue;

	    strcpy(output_if, node->output_if);
	    break;
	}
	node = node->next;
    }
    //release lock
    pthread_mutex_unlock(&rtable_lock);

    //return pointer to the interface buffer
    return output_if;
}

uint32_t gw_match(rtableNode **head, uint32_t ip)
{
    uint32_t gw = 0;

    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    //do LP matching
    rtableNode *node = *head;
    while(node != NULL) {
	if((node->ip & node->netmask) == (ip & node->netmask)) {
	    gw = node->gateway;
	    break;
	}
	node = node->next;
    }
    //release lock
    pthread_mutex_unlock(&rtable_lock);

    //return gateway
    return gw;
}


/**
 * ---------------------------------------------------------------------------
 * -------------------- CLI Functions ----------------------------------------
 * ---------------------------------------------------------------------------
 */

/** Adds a route to the appropriate routing table. */
void rtable_route_add( struct sr_instance* sr,
                       uint32_t dest, uint32_t gw, uint32_t mask,
                       void* intf,
                       int is_static_route ) 
{
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    struct sr_vns_if *interface = (struct sr_vns_if*) intf;
    insert_rtable_node(&(subsystem->rtable), dest, mask, gw, interface->name, is_static_route);

}

/** Removes the specified route from the routing table, if present. */
int rtable_route_remove( struct sr_instance* sr,
                         uint32_t dest, uint32_t mask,
                         int is_static ) 
{
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    return del_ip(&(subsystem->rtable), dest, mask, is_static);
}

/** Remove all routes from the router. */
void rtable_purge_all( struct sr_instance* sr ) 
{
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    del_route_type(&(subsystem->rtable), 0);
    del_route_type(&(subsystem->rtable), 1);
}

/** Remove all routes of a specific type from the router. */
void rtable_purge( struct sr_instance* sr, int is_static ) 
{
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    del_route_type(&(subsystem->rtable), is_static);
}

