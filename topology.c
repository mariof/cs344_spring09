#include "topology.h"
#include "pwospf.h"
#include "router.h"

#ifndef max
	#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef min
	#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif

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
    pthread_mutex_unlock(&topo_lock);
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
    pthread_mutex_unlock(&topo_lock);
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
    pthread_mutex_unlock(&topo_lock);
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
    pthread_mutex_unlock(&topo_lock);
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
    int last_seq;
    /*
     * redundant stuff
     * if(rtr == NULL) {
    pthread_mutex_unlock(&topo_lock);
   	return -1;
    }*/
    while(rtr != NULL) {
	if(rtr->router_id == router_id){
	    last_seq = rtr->last_seq;
	    pthread_mutex_unlock(&topo_lock);
	    return last_seq;
	}
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
        pthread_mutex_unlock(&topo_lock);
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
	if(time(NULL) > rtr->last_update_time && difftime(time(NULL), rtr->last_update_time) > LSU_TIMEOUT) {
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
		// go to the end first 
		while(old_ad->next != NULL) {
		    old_ad = old_ad->next;
		}
		while(old_ad->prev != NULL) {
		    prev_ad = old_ad->prev;
		    free(old_ad);
		    old_ad = prev_ad;
		}
		free(old_ad);
	    }

	    // free rtr
	    free(rtr);
		num_routers--;
	    ret = 1;
	    rtr = nxt_rtr;
	}
	else {
	    rtr = rtr->next;
	}
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
 	    pthread_mutex_unlock(&topo_lock);
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
		    pthread_mutex_unlock(&topo_lock);
		    return 0;
		}
		curr_ad->subnet = subnet;
		curr_ad->mask = mask;
	    pthread_mutex_unlock(&topo_lock);
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
	    pthread_mutex_unlock(&topo_lock);
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
    //acquire lock
    pthread_mutex_lock(&topo_lock);
    if(rtr == NULL) {
		topo_head = adj_list;
		num_routers++;
		//release lock
		pthread_mutex_unlock(&topo_lock);
		return 1;
    }

    if(rtr->router_id > adj_list->router_id) {
		adj_list->next = rtr;
		rtr->prev = adj_list;
		topo_head = adj_list;
		num_routers++;
		//release lock
		pthread_mutex_unlock(&topo_lock);
		return 1;
    }

    while(rtr->next != NULL) {
	// loop invariant: rtr->router_id <= adj_list->router_id
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
	    rtr = adj_list;
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
	}

	// free the old or new adj list
	// depending on the situation
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
	    free(old_ad);
	}
	// free rtr
	free(rtr);
	ret = 1;
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
    //release lock
    pthread_mutex_unlock(&topo_lock);
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
	if(!tight_vec[i]) {
	    min_index = i;
	    min_dist = dist_vec[i];
	    break;
	}
    }
    for(; i < n; i++) {
	if(dist_vec[i] < min_dist && !tight_vec[i]) {
	    min_index = i;
	    min_dist = dist_vec[i];
	}
    }
    return min_index;
}

static int successor(const int *adj_mat, int n, int u, int z)
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
	// add myself to topology
	addMeToTopology();

    //acquire lock
    pthread_mutex_lock(&topo_lock);
		
    int n = num_routers;

	// if there is no topology, no point in doing anything
	if(topo_head == NULL || n == 0) return;

    //malloc matrix
    //[i][j] = [i*n+j]
    int *adj_mat = malloc(sizeof(int)*n*n);
    topo_router **rtr_vec = malloc(sizeof(topo_router*)*n);
    int *dist_vec = malloc(sizeof(int)*n);
    int *parent_vec = malloc(sizeof(int)*n);
    int *tight_vec = malloc(sizeof(int)*n);

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
    for(i = 0, cur_rtr = topo_head; i < n && cur_rtr != NULL; i++, cur_rtr = cur_rtr->next) {
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
    
    // make the matrix symmetric
    for(i = 0; i < n; i++) {
	for(j = 0; j < n; j++) {
	    if(i == j)
		continue;
	    adj_mat[i*n+j] = adj_mat[j*n+i] = max(adj_mat[i*n+j], adj_mat[j*n+i]);
	}
    }
    printf("Made the matrix symmetric\n");

    // run dijkstra's algo
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int z, u;
    int s = get_index((topo_router**)rtr_vec, n, subsystem->pwospf.routerID); // source router
    if(s < 0 || s >= n) {
		printf("Failed to get index of myself...something's wrong!\n");
		exit(1);
    }
    dist_vec[s] = 0;
    parent_vec[s] = s;
    for(i = 0; i < n; i++) {
		u = get_min(dist_vec, tight_vec, n);
		if(u < 0) {
		    printf("get_min returned negative value...something's wrong - breaking out\n");
		    break;
		}
		tight_vec[u] = 1;
		if(dist_vec[u] == INT_MAX) continue;
		for(z = 0; z < n; z++) {
		    if(successor(adj_mat, n, u, z) && !tight_vec[z] 
						    && adj_mat[u*n+z] < INT_MAX 
						    && dist_vec[u]+adj_mat[u*n+z] < dist_vec[z]) {
				dist_vec[z] = dist_vec[u] + adj_mat[u*n+z];
				parent_vec[z] = u;
		    }
		}
    }
    printf("Ran Dijkstra's algo\n");

    // Sort vectors by distance - increasing order
    // simple bubble sort
    for (i = 0; i < n - 1; i++) {
	for (j = 0; j < n-1 - i; j++)
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
	printf("Reconstructing path for router %d\n", i);
		if(i == s) {
		    // I'm da ROUTER!
		    // add all my subnets to the routing table
		    struct pwospf_if *pif = subsystem->pwospf.if_list;
		    while(pif != NULL) {
				char if_name[SR_NAMELEN];
				strcpy(if_name, getIfName(pif->ip));
				//insert_shadow_node
				insert_shadow_node(&shadow, pif->ip, pif->netmask, 0, if_name, 0);
				pif = pif->next;
		    }
		    printf("Built rtable for my neighbors\n");
 		    continue;
		}

		int curr_index = i;
		while(parent_vec[curr_index] != s){// && curr_index >= 0 && curr_index < n) {
		    curr_index = parent_vec[curr_index];
		    if(curr_index < 0 || curr_index >= n) {
			//disconnected node
			break;
		    }
		}

		if(curr_index < 0 || curr_index >= n) {
		    //disconnected node
		    continue;
		}
		printf("curr_index = %d\n", curr_index);

		//curr_index is the index of the gateway router
		//add all subnets advertised by it to the routing table
		lsu_ad *nbr = rtr_vec[i]->ads;
		while(nbr != NULL) {
		    //get if,gw info from pwospf
		    char if_name[SR_NAMELEN];
		    uint32_t gw;
		    findNeighbor(rtr_vec[curr_index]->router_id, if_name, &gw);
		    printf("Got neighbor from findNeighbor - %s, 0x%x\n", if_name, gw);
		    //insert_shadow_node
		    insert_shadow_node(&shadow, nbr->subnet, nbr->mask, gw, if_name, 0);
		    printf("called insert_shadow_node\n");
			nbr = nbr->next;
		}
    }
    printf("Calling rebuild_rtable\n");
    rebuild_rtable(&(subsystem->rtable), shadow);

    // release all allocated memory
    printf("free 1\n");
    free(adj_mat);
    printf("free 2\n");
    free(rtr_vec);
    printf("free 3\n");
    free(dist_vec);
    printf("free 4\n");
    free(parent_vec);
    printf("free 5\n");
    free(tight_vec);
    printf("free 6\n");

    //release lock
    pthread_mutex_unlock(&topo_lock);
}


void addMeToTopology(){
	int i, advCnt = 0;
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// create topology data structures					
	topo_router *head = (topo_router*)malloc(sizeof(topo_router));
	head->router_id = subsystem->pwospf.routerID;
	head->area_id = subsystem->pwospf.areaID;
	head->last_seq = 0;
	head->last_update_time = (time_t)UINT_MAX;
	head->next = NULL;
	head->prev = NULL;
	head->ads = NULL;
	
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if(subsystem->ifaces[i].enabled == 0) continue;

		struct pwospf_if *pif = findPWOSPFif(&subsystem->pwospf, subsystem->ifaces[i].ip);
		if(pif == NULL) continue;
		
		pthread_mutex_lock(&pif->neighbor_lock);
		struct pwospf_neighbor *nbr = pif->neighbor_list;
		while(nbr){

			lsu_ad *node = (lsu_ad*)malloc(sizeof(lsu_ad));
			node->subnet = nbr->ip & pif->netmask;
			node->mask = pif->netmask;
			node->router_id = nbr->id;
			node->next = NULL;
			node->prev = NULL;
				
			advCnt++;
			nbr = nbr->next;
			
			if(head->ads == NULL){
				head->ads = node;
				continue;
			}
			
			// insert node into sorted list
			lsu_ad *tmp = head->ads;
			while(tmp){
				if(node->router_id < tmp->router_id) break;
				tmp = tmp->next;
			}
			if(tmp==NULL){ // find last list element and insert after it
				tmp = head->ads;
				while(tmp){
					if(tmp->next){
						tmp = tmp->next;
					}
					else{
						break;
					}
				}
				tmp->next = node;
				node->prev = tmp;
			}
			else if(tmp->prev == NULL){ // insert before first element
				node->next = tmp;
				tmp->prev = node;
				head->ads = node;
			}
			else{
				node->next = tmp;
				node->prev = tmp->prev;
				tmp->prev->next = node;
				tmp->prev = node;
			}
		}
		pthread_mutex_unlock(&pif->neighbor_lock);
	
	}
	pthread_rwlock_unlock(&subsystem->if_lock);

	head->num_ads = advCnt;
			
	// add me to topology pretending I sent myself an LSU packet
	update_lsu(head);
	
}
