#include "topology.h"
#include "pwospf.h"
#include "router.h"

int add_router(uint32_t router_id, uint16_t last_seq)
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);

    topo_router *rtr = topo_head;
    topo_router *new_rtr;
    if(rtr == NULL) {
	new_rtr = malloc(sizeof(topo_router));
	new_rtr->router_id = router_id;
	new_rtr->last_seq = last_seq;
	new_rtr->next = new_rtr->prev = NULL;
	rtr = new_rtr;
	num_routers++;
	return 1;
    }

    if(rtr->router_id > router_id) {
	new_rtr = malloc(sizeof(topo_router));
	new_rtr->router_id = router_id;
	new_rtr->last_seq = last_seq;
	new_rtr->next = new_rtr->prev = NULL;
	new_rtr->next = rtr;
	rtr->prev = new_rtr;
	rtr = new_rtr;
	num_routers++;
	return 1;
    }

    while(rtr->next != NULL) {
       	if(router_id < rtr->next->router_id) {
	    break;
	}
	rtr = rtr->next;
    }

    if(rtr->router_id == router_id) {
	// router exists
	// XXX: should the sequence number be updated?
	rtr->last_seq = last_seq;
	return 0;
    }
    else {
	// insert new router
	new_rtr = malloc(sizeof(topo_router));
	new_rtr->router_id = router_id;
	new_rtr->last_seq = last_seq;
	new_rtr->next = new_rtr->prev = NULL;
	if(rtr->next != NULL) {
	    rtr->next->prev = new_rtr;
	}
	new_rtr->next = rtr->next;
	new_rtr->prev = rtr;
	rtr->next = new_rtr;
	num_routers++;
	return 1;
    }

    //release lock
    pthread_mutex_unlock(&topo_lock);
    return -1;
}

int get_last_seq(uint32_t router_id)
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);

    topo_router *rtr = topo_head;
    if(rtr == NULL) {
	return -1;
    }
    while(rtr != NULL) {
	if(rtr->router_id == router_id)
	    return rtr->last_seq;
	rtr = rtr->next;
    }

    //release lock
    pthread_mutex_unlock(&topo_lock);
    return -1;
}

int rm_router(uint32_t router_id)
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);

    topo_router *rtr = topo_head;

    while(rtr != NULL) {
	if(rtr->router_id == router_id) {
	    // unlink the adj list from the topo db
	    if(rtr->prev != NULL) {
		rtr->prev->next = rtr->next;
	    }
	    else {
		topo_head = rtr->next;
	    }
	    if(rtr->next != NULL) {
		rtr->next->prev = rtr->prev;
	    }

	    // free the adj list
	    // free the ads first
	    lsu_ad *old_ad, *prev_ad;
	    old_ad = rtr->ads;
	    if(old_ad != NULL) {
		while(old_ad->next != NULL) {
		    old_ad = old_ad->next;
		}
		while(old_ad->prev != NULL) {
		    prev_ad = old_ad->prev;
		    free(old_ad);
		    old_ad = prev_ad;
		}
	    }
	    // free rtr
	    free(rtr);
	    return 1;
	}
	rtr = rtr->next;
    }

    //release lock
    pthread_mutex_unlock(&topo_lock);
    return 0;
}

int purge_topo()
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);
    
    int ret = 0;
    topo_router *rtr = topo_head;

    while(rtr != NULL) {
	if(difftime(time(NULL), rtr->last_update_time) > LSU_TIMEOUT) {
	    topo_router *nxt_rtr = rtr->next;

	    // unlink the adj list from the topo db
	    if(rtr->prev != NULL) {
		rtr->prev->next = rtr->next;
	    }
	    else {
		topo_head = rtr->next;
	    }
	    if(rtr->next != NULL) {
		rtr->next->prev = rtr->prev;
	    }

	    // free the adj list
	    // free the ads first
	    lsu_ad *old_ad, *prev_ad;
	    old_ad = rtr->ads;
	    if(old_ad != NULL) {
		while(old_ad->next != NULL) {
		    old_ad = old_ad->next;
		}
		while(old_ad->prev != NULL) {
		    prev_ad = old_ad->prev;
		    free(old_ad);
		    old_ad = prev_ad;
		}
	    }

	    // free rtr
	    free(rtr);
	    ret = 1;
	    rtr = nxt_rtr;
	}
	rtr = rtr->next;
    }

    //release lock
    pthread_mutex_unlock(&topo_lock);
    return ret;
}

int add_router_ad(uint32_t router_id, uint32_t subnet, uint32_t mask, uint32_t nbr_router_id)
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);

    topo_router *rtr = topo_head;

    while(rtr != NULL) {
	rtr = rtr->next;
	if(rtr->router_id == router_id) {
	    lsu_ad *curr_ad = rtr->ads;
	    lsu_ad *new_ad;
	    if(curr_ad == NULL) {
		new_ad = malloc(sizeof(lsu_ad));
		new_ad->router_id = nbr_router_id;
		new_ad->subnet = subnet;
		new_ad->mask = mask;
		new_ad->next = new_ad->prev = NULL;
		curr_ad = new_ad;
		return 1;
	    }
	    if(curr_ad->router_id > nbr_router_id) {
		new_ad = malloc(sizeof(lsu_ad));
		new_ad->router_id = nbr_router_id;
		new_ad->subnet = subnet;
		new_ad->mask = mask;
		new_ad->next = new_ad->prev = NULL;

		curr_ad->prev = new_ad;
		new_ad->next = curr_ad;
		rtr->ads = new_ad;
	    }

	    while(curr_ad->next != NULL) {
		if(curr_ad->next->router_id > nbr_router_id)
		    break;
		curr_ad = curr_ad->next;
	    }
	    if(curr_ad->router_id == nbr_router_id) {
		// router ad exists
		if(curr_ad->subnet == subnet && curr_ad->mask == mask) {
		    return 0;
		}
		curr_ad->subnet = subnet;
		curr_ad->mask = mask;
		return 1;
	    }
	    else {
		new_ad = malloc(sizeof(lsu_ad));
		new_ad->router_id = nbr_router_id;
		new_ad->subnet = subnet;
		new_ad->mask = mask;
		new_ad->next = new_ad->prev = NULL;

		if(curr_ad->next != NULL)
		    curr_ad->next->prev = new_ad;
		new_ad->next = curr_ad->next;
		curr_ad->next = new_ad;
		new_ad->prev = curr_ad;
		return 1;
	    }
	}
    }
    //release lock
    pthread_mutex_unlock(&topo_lock);
    return -1;
}

int update_lsu(topo_router *adj_list)
{
    int ret = 0;
    topo_router *rtr = topo_head;
    if(rtr == NULL) {
	rtr = adj_list;
	num_routers++;
	return 1;
    }

    if(rtr->router_id > adj_list->router_id) {
	adj_list->next = rtr;
	rtr->prev = adj_list;
	rtr = adj_list;
	num_routers++;
	return 1;
    }

    while(rtr->next != NULL) {
       	if(adj_list->router_id < rtr->next->router_id) {
	    break;
	}
	rtr = rtr->next;
    }

    if(rtr->router_id == adj_list->router_id) {
	// router exists
	// compare the neighbor list with the existing neighbor list
	lsu_ad *old_ad = rtr->ads;
	lsu_ad *new_ad = adj_list->ads;
	lsu_ad *prev_ad;
	while(old_ad != NULL && new_ad != NULL) {
	    if(old_ad->router_id != new_ad->router_id || old_ad->subnet != new_ad->subnet
		    || old_ad->mask != new_ad->mask) {
		break;
	    }
	    old_ad = old_ad->next;
	    new_ad = new_ad->next;
	}
	if (old_ad == new_ad) { // == NULL
	    // No change!
	    // just update the last received sequence number
	    rtr->last_seq = adj_list->last_seq;
	    ret = 0;
	}
	else {
	    // adj list has changed
	    adj_list->next = rtr->next;
	    adj_list->prev = rtr->prev;
	    if(rtr->prev != NULL) {
		rtr->prev->next = adj_list;
	    }
	    else {
		topo_head = adj_list;
	    }
	    if(rtr->next != NULL) {
		rtr->next->prev = adj_list;
	    }
	    // free the old adj list
	    // free the ads first
	    old_ad = rtr->ads;
	    if(old_ad != NULL) {
		while(old_ad->next != NULL) {
		    old_ad = old_ad->next;
		}
		while(old_ad->prev != NULL) {
		    prev_ad = old_ad->prev;
		    free(old_ad);
		    old_ad = prev_ad;
		}
	    }
	    // free rtr
	    free(rtr);
	    ret = 1;
	}
    }
    else {
	// insert new router
	if(rtr->next != NULL) {
	    rtr->next->prev = adj_list;
	}
	adj_list->next = rtr->next;
	adj_list->prev = rtr;
	rtr->next = adj_list;
	num_routers++;
	ret = 1;
    }
    return ret;
}

/* 
 * ------------------------------- helper functions to run dijkstra's algo ------------------
 * */

static int get_index(topo_router **rtr_vec, int n, uint32_t router_id) {
    int i;
    for(i = 0; i < n; i++) {
	if(rtr_vec[i]->router_id == router_id)
	    return i;
    }
    return -1;
}

static int get_min(const int *dist_vec, const int *tight_vec, int n) {
    int i, min_index = -1, min_dist = INT_MAX;
    for(i = 0; i < n; i++) {
	if(dist_vec[i] < min_dist && !tight_vec[i]) {
	    min_index = i;
	    min_dist = dist_vec[i];
	}
    }
    return min_index;
}

static int successor(int *adj_mat, int n, int u, int z)
{
    return (adj_mat[u*n+z] < INT_MAX && u != z);
}

/*
 * basically the same implementation as the one in routingTable, but
 * without locks, and
 * table entry comparison is done based only on ip&netmask
 */
static void insert_shadow_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t gateway, const char* output_if, int is_static)
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

    //Check if the list is empty
    if(*head == NULL) {
	(*head) = node;
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
	if(((cnode->ip)&(cnode->netmask)) == (ip&netmask)) {
	    free(node);
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

    return;
}

void update_rtable()
{
    int n = num_routers;
    //malloc matrix
    //[i][j] = [i*n+j]
    int *adj_mat = malloc(sizeof(int)*n*n);
    topo_router **rtr_vec = malloc(sizeof(topo_router*)*n);
    int *dist_vec = malloc(sizeof(int)*n);
    int *parent_vec = malloc(sizeof(int)*n);
    int *tight_vec = malloc(sizeof(int)*n);

    //acquire lock
    pthread_mutex_lock(&topo_lock);

    // fill rtr_vec, dist_vec, parent_vec, tight_vec
    int i, j;
    topo_router *cur_rtr = topo_head;
    for(i = 0; i < n; i++, cur_rtr = cur_rtr->next) {
	rtr_vec[i] = cur_rtr;
	dist_vec[i] = INT_MAX;
	parent_vec[i] = -1;
	tight_vec[i] = 0;
    }

    // fill adj_mat
    for(i = 0, cur_rtr = topo_head; i < n; i++, cur_rtr = cur_rtr->next) {
	lsu_ad *cur_ad = cur_rtr->ads;
	for(j = 0; j < n; j++) {
	    if(cur_ad == NULL) {
		adj_mat[i*n+j] = INT_MAX;
		continue;
	    }
	    if(cur_ad->router_id != rtr_vec[j]->router_id) {
		adj_mat[i*n+j] = INT_MAX;
		continue;
	    }
	    else if(cur_ad->router_id == rtr_vec[j]->router_id) {
		adj_mat[i*n+j] = 1;
		cur_ad = cur_ad->next;
	    }
	}
    }

    // print adj_mat
    /*
    printf("----------------------------------------------\n");
    printf("----------------------------------------------\n");
    for(i = 0, cur_rtr = topo_head; i < n; i++, cur_rtr = cur_rtr->next) {
	printf("%d, ", cur_rtr->router_id);
    }
    printf("\n");
    printf("----------------------------------------------\n");
    for(i = 0; i < n; i++) {
	for(j = 0; j < n; j++) {
	    printf("%d\t", adj_mat[i*n+j]);
	}
	printf("\n");
    }
    printf("----------------------------------------------\n");
    printf("----------------------------------------------\n");
    */

    // run dijkstra's algo
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int z, u;
    int s = get_index((topo_router**)rtr_vec, n, subsystem->pwospf.routerID); // source router
    if(s < 0) {
	printf("Failed to get index of myself...something's wrong!\n");
    }
    dist_vec[s] = 0;
    parent_vec[s] = s;
    for(i = 0; i < n; i++) {
	u = get_min(dist_vec, tight_vec, n);
	tight_vec[u] = 1;
	if(dist_vec[u] == INT_MAX)
	    continue;
	for(z = 0; z < n; z++) {
	    if(successor(adj_mat, n, u, z) && !tight_vec[z] 
		    && adj_mat[u*n+z] < INT_MAX 
		    && dist_vec[u]+adj_mat[u*n+z] < dist_vec[z]) {
		dist_vec[z] = dist_vec[u] + adj_mat[u*n+z];
		parent_vec[z] = u;
	    }
	}
    }


    // Sort vectors by distance - increasing order
    // simple bubble sort
    for (i=0; i<n-1; i++) {
	for (j=0; j<n-1-i; j++)
	    if (dist_vec[j+1] < dist_vec[j]) {  /* compare the two neighbors */
		int tmp_int;
		topo_router *tmp_rtr;
		// swap dist_vec neighbors
		tmp_int = dist_vec[j];
		dist_vec[j] = dist_vec[j+1];
		dist_vec[j+1] = tmp_int;
		// swap rtr_vec neighbors
		tmp_rtr = rtr_vec[j];
		rtr_vec[j] = rtr_vec[j+1];
		rtr_vec[j+1] = tmp_rtr;
		// swap parent_vec neighbors
		tmp_int = parent_vec[j];
		parent_vec[j] = parent_vec[j+1];
		parent_vec[j+1] = tmp_int;
	    }
    }

    
    // For each router, reconstruct path
    rtableNode *shadow = NULL;
    for(i = 0; i < n; i++) {
	if(i == s) {
	    // I'm da ROUTER!
	    // add all my subnets to the routing table
	    lsu_ad *nbr = rtr_vec[i]->ads;
	    while(nbr != NULL) {
		//get if,gw info from pwospf
		//insert_shadow_node
	    }
	    continue;
	}

	int curr_index = i;
	while(parent_vec[curr_index] != s) {
	    curr_index = parent_vec[curr_index];
	}
	//curr_index is the index of the gateway router
	//add all subnets advertised by it to the routing table
	lsu_ad *nbr = rtr_vec[i]->ads;
	while(nbr != NULL) {
	    //get if,gw info from pwospf
	    //insert_shadow_node
	}
    }
    rebuild_rtable(&(subsystem->rtable), shadow);

    // release all allocated memory
    free(adj_mat);
    free(rtr_vec);
    free(dist_vec);
    free(parent_vec);
    free(tight_vec);

    //release lock
    pthread_mutex_unlock(&topo_lock);
}
