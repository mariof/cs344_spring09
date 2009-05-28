#include "router.h"
#include "routingTable.h"

void insert_rtable_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t* gateway, char** output_if, int out_cnt, int is_static)
{
	int i;
	
	if(out_cnt < 1) return;
	
    //check output_if size
    for(i = 0; i < out_cnt; i++){
	    if(strlen(output_if[i]) >= SR_NAMELEN) {
			return;
	    }
	}

    //create new node
    rtableNode *node = (rtableNode*) malloc(sizeof(rtableNode));
    node->ip = ip;
    node->netmask = netmask;
    node->out_cnt = out_cnt;
    node->gateway = (uint32_t*)malloc(sizeof(uint32_t)*out_cnt);
    node->output_if = (char**)malloc(sizeof(char*)*out_cnt);
    for(i = 0; i < out_cnt; i++) node->output_if[i] = (char*)malloc(sizeof(char)*SR_NAMELEN);
    for(i = 0; i < out_cnt; i++) node->gateway[i] = gateway[i];
    for(i = 0; i < out_cnt; i++) strcpy(node->output_if[i], output_if[i]);
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
    if(netmask > cnode->netmask || (netmask == cnode->netmask && (ip&netmask) > (cnode->ip&cnode->netmask))) {
		*head = node;
		node->next = cnode;
		cnode->prev = node;
    }
    else {
		while(cnode->next != NULL) {
		    if(netmask > cnode->next->netmask || (netmask == cnode->next->netmask && (ip&netmask) > (cnode->next->ip & cnode->next->netmask))) {
				break;
		    }
		    cnode = cnode->next;
		}

		//check for equality to prevent adding duplicate nodes
		if((cnode->ip&cnode->netmask) == (ip&netmask) && (cnode->is_static == is_static)) {
			if(cnode->out_cnt != out_cnt){
				for(i = 0; i < cnode->out_cnt; i++) free(cnode->output_if[i]);
				free(cnode->output_if);
				free(cnode->gateway);
			    cnode->out_cnt = out_cnt;
			    cnode->gateway = (uint32_t*)malloc(sizeof(uint32_t)*cnode->out_cnt);
			    cnode->output_if = (char**)malloc(sizeof(char*)*cnode->out_cnt);
			    for(i = 0; i < cnode->out_cnt; i++) cnode->output_if[i] = (char*)malloc(sizeof(char)*SR_NAMELEN);							
			}
			for(i = 0; i < out_cnt; i++){
				cnode->gateway[i] = gateway[i];
				strcpy(cnode->output_if[i], output_if[i]);
			}
			for(i = 0; i < node->out_cnt; i++) free(node->output_if[i]);
			free(node->output_if);
			free(node->gateway);
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


void merge_rtable_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t* gateway, char** output_if, int out_cnt, int is_static)
{
	int i;
	
	if(out_cnt < 1) return;
	
    //check output_if size
    for(i = 0; i < out_cnt; i++){
	    if(strlen(output_if[i]) >= SR_NAMELEN) {
			return;
	    }
	}

    //create new node
    rtableNode *node = (rtableNode*) malloc(sizeof(rtableNode));
    node->ip = ip;
    node->netmask = netmask;
    node->out_cnt = out_cnt;
    node->gateway = (uint32_t*)malloc(sizeof(uint32_t)*out_cnt);
    node->output_if = (char**)malloc(sizeof(char*)*out_cnt);
    for(i = 0; i < out_cnt; i++) node->output_if[i] = (char*)malloc(sizeof(char)*SR_NAMELEN);
    for(i = 0; i < out_cnt; i++) node->gateway[i] = gateway[i];
    for(i = 0; i < out_cnt; i++) strcpy(node->output_if[i], output_if[i]);
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
    if(netmask > cnode->netmask || (netmask == cnode->netmask && (ip&netmask) > (cnode->ip&cnode->netmask))) {
		*head = node;
		node->next = cnode;
		cnode->prev = node;
    }
    else {
		while(cnode->next != NULL) {
		    if(netmask > cnode->next->netmask || (netmask == cnode->next->netmask && (ip&netmask) > (cnode->next->ip & cnode->next->netmask))) {
				break;
		    }
		    cnode = cnode->next;
		}

		//check for equality to prevent adding duplicate nodes
		if((cnode->ip&cnode->netmask) == (ip&netmask)) {
			int old_cnt = cnode->out_cnt;
		    cnode->out_cnt += out_cnt;
		    cnode->gateway = (uint32_t*)realloc(cnode->gateway, sizeof(uint32_t)*cnode->out_cnt);
		    cnode->output_if = (char**)realloc(cnode->output_if, sizeof(char*)*cnode->out_cnt);
		    for(i = old_cnt; i < cnode->out_cnt; i++) cnode->output_if[i] = (char*)malloc(sizeof(char)*SR_NAMELEN);							
			for(i = 0; i < out_cnt; i++){
				cnode->gateway[old_cnt + i] = gateway[i];
				strcpy(cnode->output_if[old_cnt + i], output_if[i]);
			}
			for(i = 0; i < node->out_cnt; i++) free(node->output_if[i]);
			free(node->output_if);
			free(node->gateway);
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


int del_ip(rtableNode **head, uint32_t ip, uint32_t netmask, int is_static)
{
	int i;
    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    // search node
    rtableNode *node = *head;
    while(node != NULL) {
		if( ( (node->ip & node->netmask) == (ip & netmask) ) && ( node->is_static == is_static ) ) {
		    //delete node
		    if(node->prev != NULL) {
				(node->prev)->next = node->next;
		    }
		    if(node->next != NULL) {
				(node->next)->prev = node->prev;
		    }
			for(i = 0; i < node->out_cnt; i++) free(node->output_if[i]);
			free(node->output_if);
			free(node->gateway);
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
	int i;
    //acquire lock
    pthread_mutex_lock(&rtable_lock);

    // search node
    rtableNode *node = *head;
    while(node != NULL) {
	if(node->is_static == is_static) {
	    rtableNode *nxt_node = node->next;
	    //delete node
	    if(node->prev != NULL) {
		(node->prev)->next = node->next;
	    }
	    else {
		*head = node->next;
	    }
	    if(node->next != NULL) {
		(node->next)->prev = node->prev;
	    }
		for(i = 0; i < node->out_cnt; i++) free(node->output_if[i]);
		free(node->output_if);
		free(node->gateway);
	    free(node);
	    node = nxt_node;
	    continue;
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
	    pthread_rwlock_rdlock(&subsystem->if_lock);
	    for(i = 0; i < subsystem->num_ifaces; i++) {
			if(!strcmp(subsystem->ifaces[i].name, output_if)) {
			    if(!(subsystem->ifaces[i].enabled)) {
					iface_disabled = 1;
			    }
			    break;
			}
	    }
	    pthread_rwlock_unlock(&subsystem->if_lock);

	    if(iface_disabled)
		continue;

	    strcpy(output_if, node->output_if[0]);
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
	    gw = node->gateway[0];
	    break;
	}
	node = node->next;
    }
    //release lock
    pthread_mutex_unlock(&rtable_lock);

	if(gw == 0) gw = ip;

    //return gateway
    return gw;
}

void rebuild_rtable(rtableNode **head, rtableNode *shadow_table)
{
	int i;
    // acquire lock
    pthread_mutex_lock(&rtable_lock);

    // purge all the dynamic entries
    int is_static = 0;
    rtableNode *node = *head;
    while(node != NULL) {
	if(node->is_static == is_static) {
	    rtableNode *nxt_node = node->next;
	    //delete node
	    if(node->prev != NULL) {
		(node->prev)->next = node->next;
	    }
	    else {
		*head = node->next;
	    }
	    if(node->next != NULL) {
		(node->next)->prev = node->prev;
	    }
		for(i = 0; i < node->out_cnt; i++) free(node->output_if[i]);
		free(node->output_if);
		free(node->gateway);	    
	    free(node);
	    node = nxt_node;
	    continue;
	}
	node = node->next;
    }

    // add all the entries from the shadow table
    node = shadow_table;
    rtableNode *nxt_node = NULL;

    while(node != NULL) {
	nxt_node = node->next;

	//Check if the list is empty
	if(*head == NULL) {
	    (*head) = node;
	    node->next = NULL;
	    node = nxt_node;
	    continue;
	}

	//scan the list until you hit the right netmask
	rtableNode *cnode = *head;
	if(node->netmask > cnode->netmask || (node->netmask == cnode->netmask && (node->ip & node->netmask) > (cnode->ip & cnode->netmask))) {
	    *head = node;
	    node->next = cnode;
	    cnode->prev = node;
	}
	else {
	    while(cnode->next != NULL) {
			if(node->netmask > cnode->next->netmask || (node->netmask == cnode->next->netmask && (node->ip & node->netmask) > (cnode->next->ip & cnode->next->netmask))) {
			    break;
			}
			cnode = cnode->next;
	    }

	    //check for equality to prevent adding duplicate nodes
	    if(((cnode->ip & cnode->netmask) == (node->ip & node->netmask)) && (cnode->is_static == node->is_static)) {
			if(cnode->out_cnt != node->out_cnt){
				for(i = 0; i < cnode->out_cnt; i++) free(cnode->output_if[i]);
				free(cnode->output_if);
				free(cnode->gateway);
			    cnode->out_cnt = node->out_cnt;
			    cnode->gateway = (uint32_t*)malloc(sizeof(uint32_t)*cnode->out_cnt);
			    node->output_if = (char**)malloc(sizeof(char*)*cnode->out_cnt);
			    for(i = 0; i < cnode->out_cnt; i++) cnode->output_if[i] = (char*)malloc(sizeof(char)*SR_NAMELEN);							
			}
			for(i = 0; i < node->out_cnt; i++){
				cnode->gateway[i] = node->gateway[i];
				strcpy(cnode->output_if[i], node->output_if[i]);
			}
			for(i = 0; i < node->out_cnt; i++) free(node->output_if[i]);
			free(node->output_if);
			free(node->gateway);
			free(node);
			node = nxt_node;
			continue;
	    }

	    // insert new node
	    if(cnode->next != NULL) {
			(cnode->next)->prev = node;
	    }
	    node->next = cnode->next;
	    node->prev = cnode;
	    cnode->next = node;
	}
	node = nxt_node;
    }

	#ifdef _CPUMODE_
	// update hw table
	writeRoutingTable();
	#endif // _CPUMODE_

    // release lock
    pthread_mutex_unlock(&rtable_lock);
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
    char* tmp_if = (char*)malloc(sizeof(char)*SR_NAMELEN);
    strcpy(tmp_if, interface->name);
    insert_rtable_node(&(subsystem->rtable), dest, mask, &gw, &tmp_if, 1, is_static_route);
	free(tmp_if);
}

/** Adds a multipath route (i.e. merges new route with old ones) */
void rtable_route_addm( struct sr_instance* sr,
                       uint32_t dest, uint32_t gw, uint32_t mask,
                       void* intf,
                       int is_static_route ) 
{
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    struct sr_vns_if *interface = (struct sr_vns_if*) intf;
    char* tmp_if = (char*)malloc(sizeof(char)*SR_NAMELEN);
    strcpy(tmp_if, interface->name);
    merge_rtable_node(&(subsystem->rtable), dest, mask, &gw, &tmp_if, 1, is_static_route);
	free(tmp_if);
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

