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
		if( (time(NULL) > rtr->last_update_time) && (time(NULL) - rtr->last_update_time > LSU_TIMEOUT) ) {
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


int flush_topo()
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);
    
    int ret = 0;
    topo_router *rtr = topo_head;

    while(rtr != NULL) {
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
			rtr->last_update_time = adj_list->last_update_time;
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
			ret = 1;
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
static void insert_shadow_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t* gateway, char** output_if, int out_cnt, int is_static, int entry_index)
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
    node->entry_index = entry_index;
    for(i = 0; i < out_cnt; i++) node->output_if[i] = (char*)malloc(sizeof(char)*SR_NAMELEN);
    for(i = 0; i < out_cnt; i++) node->gateway[i] = gateway[i];
    for(i = 0; i < out_cnt; i++) strcpy(node->output_if[i], output_if[i]);
    node->is_static = is_static;
    node->next = node->prev = NULL;
    
    //Check if the list is empty
    if(*head == NULL) {
		(*head) = node;
		return;
    }

    //scan the list until you hit the right netmask
    rtableNode *cnode = *head;
    if(netmask > cnode->netmask || (netmask == cnode->netmask && (ip & netmask) > (cnode->ip & cnode->netmask))) {
		*head = node;
		node->next = cnode;
		cnode->prev = node;
    }
    else {
		while(cnode->next != NULL) {
		    if(netmask > cnode->next->netmask || (netmask == cnode->next->netmask && (ip & netmask) > (cnode->next->ip & cnode->next->netmask))) {
				break;
		    }
		    cnode = cnode->next;
		}

		//check for equality to prevent adding duplicate nodes (this is tricky, because we kinda need duplicates for fast reroute) 
		if((cnode->ip & cnode->netmask) == (ip & netmask)) {
			int tmp_flag = 1;
			while((cnode->ip & cnode->netmask) == (ip & netmask)){			
				if(cnode->next){	
					cnode = cnode->next;
				}
				else{
					tmp_flag = 0;
					break;
				}
			}
			if(tmp_flag) cnode = cnode->prev;
			if(cnode->entry_index >= entry_index){
				for(i = 0; i < node->out_cnt; i++) free(node->output_if[i]);
				free(node->output_if);
				free(node->gateway);
			    free(node);
			    return;
			}
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
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// add myself to topology
	addMeToTopology();

    //acquire lock
    pthread_mutex_lock(&topo_lock);
		
    int n = num_routers;
	int nif;
	
	pthread_mutex_lock(&subsystem->mode_lock);
	
	// if advanced feature is enabled, allocate more memory
	if(subsystem->mode != 0)
	 	nif = subsystem->num_ifaces;
	else	
		nif = 1;
	
	// if there is no topology, no point in doing anything
	if(topo_head == NULL || n == 0) return;

    //malloc matrix
    //[i][j] = [i*n+j]
    int *adj_mat = malloc(sizeof(int)*n*n*nif);
    topo_router **rtr_vec = malloc(sizeof(topo_router*)*n*nif);
    topo_router **rtr_vec_tot = malloc(sizeof(topo_router*)*n);
    int *dist_vec = malloc(sizeof(int)*n*nif);
    int *dist_vec_tot = malloc(sizeof(int)*n);
    int *parent_vec = malloc(sizeof(int)*n*nif);
    int *tight_vec = malloc(sizeof(int)*n*nif);

    // fill rtr_vec, dist_vec, parent_vec, tight_vec, adj_mat
    int i, j, ai;
    topo_router *cur_rtr;
    
    int s;
    
    cur_rtr = topo_head;
    for(i = 0; i < n; i++, cur_rtr = cur_rtr->next){
		rtr_vec_tot[i] = cur_rtr;
		dist_vec_tot[i] = INT_MAX;    
    }
    
    for(ai = 0; ai < nif; ai++){
	    cur_rtr = topo_head;

	    for(i = 0; i < n; i++, cur_rtr = cur_rtr->next) {
			rtr_vec[ai*n+i] = cur_rtr;
			dist_vec[ai*n+i] = INT_MAX;
			parent_vec[ai*n+i] = -1;
			tight_vec[ai*n+i] = 0;
			for(j = 0; j < n; j++) {
				adj_mat[ai*n*n+i*n+j] = INT_MAX;
			}
	    }
	    //printf("Initialized variables\n");

	    // fill adj_mat
	    for(i = 0, cur_rtr = topo_head; i < n && cur_rtr != NULL; i++, cur_rtr = cur_rtr->next) {
			//printf("populating adj_mat: i = %d\n", i);
			lsu_ad *cur_ad = cur_rtr->ads;
			for(j = 0; j < n && cur_ad != NULL; j++) {
			    //printf("populating adj_mat: j = %d\n", j);
			    while(cur_ad != NULL && cur_ad->router_id < rtr_vec[ai*n+j]->router_id) {
					cur_ad = cur_ad->next;
			    }
			    if(cur_ad == NULL)
					break;
				if(cur_rtr->router_id == subsystem->pwospf.routerID){
					if((nif > 1) && (cur_ad->subnet & cur_ad->mask) != (subsystem->ifaces[ai].ip & subsystem->ifaces[ai].mask) ){
						cur_ad = cur_ad->next;
						continue;
					} 
				}
			    if(cur_ad->router_id == rtr_vec[ai*n+j]->router_id) {
					adj_mat[ai*n*n+i*n+j] = 1;
					cur_ad = cur_ad->next;
			    }
			}
	    }

	    // print topology
	    printf("**********************************************\n");
		topo_router *p_router = topo_head;
		while(p_router){
			printf("%x:: ", p_router->router_id);
			lsu_ad *p_ad = p_router->ads;
			while(p_ad){
				printf("%x ", p_ad->router_id);
				p_ad = p_ad->next;
			}
			printf("\n");
			p_router = p_router->next;
		}
	    printf("**********************************************\n");

	    // print adj_mat
	    printf("----------------------------------------------\n");
	    printf("----------------------------------------------\n");
	    for(i = 0, cur_rtr = topo_head; i < n; i++, cur_rtr = cur_rtr->next) {
			printf("%x, ", cur_rtr->router_id);
	    }
	    printf("\n");
	    printf("----------------------------------------------\n");
	    printf("interface: %d\n", ai);
	    for(i = 0; i < n; i++) {
			for(j = 0; j < n; j++) {
			    printf("%d\t", adj_mat[ai*n*n+i*n+j]);
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
			    adj_mat[ai*n*n+i*n+j] = adj_mat[ai*n*n+j*n+i] = max(adj_mat[ai*n*n+i*n+j], adj_mat[ai*n*n+j*n+i]);
			}
	    }
	    //printf("Made the matrix symmetric\n");

	    // run dijkstra's algo
	    int z, u;
	    s = get_index((topo_router**)(&rtr_vec[ai*n]), n, subsystem->pwospf.routerID); // source router
	    if(s < 0 || s >= n) {
			printf("Failed to get index of myself...something's wrong!\n");
			return;
	    }
	    dist_vec[ai*n+s] = 0;
	    parent_vec[ai*n+s] = s;
	    for(i = 0; i < n; i++) {
			u = get_min(&dist_vec[ai*n], &tight_vec[ai*n], n);
			if(u < 0) {
			    printf("get_min returned negative value...something's wrong - breaking out\n");
			    break;
			}
			tight_vec[ai*n+u] = 1;
			if(dist_vec[ai*n+u] == INT_MAX) continue;
			for(z = 0; z < n; z++) {
			    if(successor(&adj_mat[ai*n*n], n, u, z) && !tight_vec[ai*n+z] 
							    && adj_mat[ai*n*n+u*n+z] < INT_MAX 
							    && dist_vec[ai*n+u]+adj_mat[ai*n*n+u*n+z] < dist_vec[ai*n+z]) {
					dist_vec[ai*n+z] = dist_vec[ai*n+u] + adj_mat[ai*n*n+u*n+z];
					parent_vec[ai*n+z] = u;
			    }
			}
	    }
	    //printf("Ran Dijkstra's algo\n");
	    
	    // calculate total minimum distances (over all interfaces)
	    for(i = 0; i < n; i++){
			//int loc_i = get_index((topo_router**)&rtr_vec[ai*n], n, rtr_vec_tot[i]->router_id); // source router
		    if(dist_vec[ai*n+i] < dist_vec_tot[i]) dist_vec_tot[i] = dist_vec[ai*n+i];
		}
	    
	    // Sort vectors by distance - increasing order
	    // simple bubble sort
	    for (i = 0; i < n - 1; i++) {
		for (j = 0; j < n-1 - i; j++)
		    if (dist_vec[ai*n+j+1] < dist_vec[ai*n+j]) {  /* compare the two neighbors */
				int tmp_int;
				topo_router *tmp_rtr;
				// swap dist_vec neighbors
				tmp_int = dist_vec[ai*n+j];
				dist_vec[ai*n+j] = dist_vec[ai*n+j+1];
				dist_vec[ai*n+j+1] = tmp_int;
				// swap rtr_vec neighbors
				tmp_rtr = rtr_vec[ai*n+j];
				rtr_vec[ai*n+j] = rtr_vec[ai*n+j+1];
				rtr_vec[ai*n+j+1] = tmp_rtr;
				// swap parent_vec neighbors
				tmp_int = parent_vec[ai*n+j];
				parent_vec[ai*n+j] = parent_vec[ai*n+j+1];
				parent_vec[ai*n+j+1] = tmp_int;
				int k = 0;
				for(k = 0; k < n; k++) {
					if(parent_vec[ai*n+k] == j)
						parent_vec[ai*n+k] = j+1;
					else if(parent_vec[ai*n+k] == j+1)
						parent_vec[ai*n+k] = j;
				}
		    }
	    }
	}    

	printf("^^^^^^^^^^ dist_vec ^^^^^^^^^^\n");
	for(ai = 0; ai < nif; ai++){
		for(i = 0; i < n; i++) printf("%d\t", dist_vec[ai*n+i]);
	    printf("\n");
	}
	printf("^^^^^^^^^^ dist_vec_tot ^^^^^^^^^^\n");
	for(i = 0; i < n; i++) printf("%d\t", dist_vec_tot[i]);
    printf("\n");
	printf("^^^^^^^^^^ rtr_vec ^^^^^^^^^^\n");
	for(ai = 0; ai < nif; ai++){
		for(i = 0; i < n; i++) printf("%x\t", rtr_vec[ai*n+i]->router_id);
	    printf("\n");
	}
	printf("^^^^^^^^^^ rtr_vec_tot ^^^^^^^^^^\n");
	for(i = 0; i < n; i++) printf("%x\t", rtr_vec_tot[i]->router_id);
    printf("\n");

	// sort total distances
    // simple bubble sort
    for (i = 0; i < n - 1; i++) {
		for (j = 0; j < n-1 - i; j++)
		    if (dist_vec_tot[j+1] < dist_vec_tot[j]) {  /* compare the two neighbors */
				int tmp_int;
				topo_router *tmp_rtr;
				// swap dist_vec neighbors
				tmp_int = dist_vec_tot[j];
				dist_vec_tot[j] = dist_vec_tot[j+1];
				dist_vec_tot[j+1] = tmp_int;
				// swap rtr_vec neighbors
				tmp_rtr = rtr_vec_tot[j];
				rtr_vec_tot[j] = rtr_vec_tot[j+1];
				rtr_vec_tot[j+1] = tmp_rtr;
		    }
    }
        
    s = get_index((topo_router**)rtr_vec_tot, n, subsystem->pwospf.routerID); // source router
    // For each router, reconstruct path
    rtableNode *shadow = NULL;
    for(i = 0; i < n; i++) {
		//printf("Reconstructing path for router %u\n", i);
		if(rtr_vec_tot[i]->router_id == subsystem->pwospf.routerID) {
		    // I'm da ROUTER!
		    // add all my subnets to the routing table
		    struct pwospf_if *pif = subsystem->pwospf.if_list;
		    while(pif != NULL) {
				char if_name[SR_NAMELEN];
				if(isEnabled(pif->ip)){
					strcpy(if_name, getIfName(pif->ip));
					//insert_shadow_node
					uint32_t null_gw = 0;
				    char *tmp_if = (char*)malloc(sizeof(char)*SR_NAMELEN);
				    strcpy(tmp_if, if_name);
					insert_shadow_node(&shadow, pif->ip, pif->netmask, &null_gw, &tmp_if, 1, 0, 0);
					free(tmp_if);
				}
				pif = pif->next;
		    }
		    //printf("Built rtable for my neighbors\n");
 		    continue;
		}

	    int *curr_index = (int*)malloc(sizeof(int)*nif);
	    int cont_flag = 0;
	    for(ai = 0; ai < nif; ai++){
			int loc_i = get_index((topo_router**)&rtr_vec[ai*n], n, rtr_vec_tot[i]->router_id); // target router
	    	if(rtr_vec[ai*n+loc_i]->router_id == subsystem->pwospf.routerID){
	    		cont_flag = 1;
	    		curr_index[ai] = -1;
	    		break;	
	    	}
			curr_index[ai] = loc_i;
			int hack_index = -1;
			while(parent_vec[ai*n+curr_index[ai]] != s){
			    if(hack_index == curr_index[ai]) break;
			    hack_index = curr_index[ai];
			    curr_index[ai] = parent_vec[ai*n+curr_index[ai]];
			    if(curr_index[ai] < 0 || curr_index[ai] >= n) {
					//disconnected node
					break;
			    }
			}
			if(parent_vec[ai*n+curr_index[ai]] < 0 || parent_vec[ai*n+curr_index[ai]] >= n) {
			    //disconnected node
			    curr_index[ai] = -1;
			}
 
		}
		if(cont_flag) continue;
		
		int fast_reroute_cnt = 0;
		while(1){	// this will loop once for normal mode, twice for fast reroute
			int min_dist = INT_MAX;
			if(subsystem->mode & 0x1){ // if multipath
				int entry_cnt = 0;
				for(ai = 0; ai < nif; ai++){
					if(curr_index[ai] < 0) continue;
					int loc_i = get_index((topo_router**)&rtr_vec[ai*n], n, rtr_vec_tot[i]->router_id); // target rtr
					if(dist_vec[ai*n+loc_i] < min_dist){
						min_dist = dist_vec[ai*n+loc_i];
						entry_cnt = 1;
					}
					else if(min_dist != INT_MAX  &&  dist_vec[ai*n+loc_i] == min_dist){
						entry_cnt++;
					}
				}
				if(entry_cnt > 0  &&  min_dist != INT_MAX){
					uint32_t* m_gw = (uint32_t*)malloc(sizeof(uint32_t)*entry_cnt);
					char** m_ifname = (char**)malloc(sizeof(char*)*entry_cnt);
					int j;
					for (j = 0; j < entry_cnt; j++) m_ifname[j] = (char*)malloc(sizeof(char)*SR_NAMELEN);
			
					int entry_index = 0;
					for(ai = 0; ai < nif; ai++){
						int loc_i = get_index((topo_router**)&rtr_vec[ai*n], n, rtr_vec_tot[i]->router_id); // target router
						if(dist_vec[ai*n+loc_i] == min_dist){
							if(curr_index[ai] < 0) continue;
							int ret = findNeighbor(rtr_vec[ai*n+curr_index[ai]]->router_id, m_ifname[entry_index], &m_gw[entry_index]);
							if(!ret) continue;
							entry_index++;
							dist_vec[ai*n+loc_i] = INT_MAX;
						}		
					}

					lsu_ad *nbr = rtr_vec_tot[i]->ads;
					while(nbr != NULL && entry_index > 0) {
					    //insert_shadow_node
					    insert_shadow_node(&shadow, nbr->subnet, nbr->mask, m_gw, m_ifname, entry_index, 0, fast_reroute_cnt);
					    nbr = nbr->next;
					}

					for (j = 0; j < entry_cnt; j++) free(m_ifname[j]);	
					free(m_ifname);		
					free(m_gw);
				}
			}
			else{
				int min_index = -1;
				for(ai = 0; ai < nif; ai++){
					if(curr_index[ai] < 0) continue;
					int loc_i = get_index((topo_router**)&rtr_vec[ai*n], n, rtr_vec_tot[i]->router_id); // source router
					if(dist_vec[ai*n+loc_i] < min_dist){
						min_dist = dist_vec[ai*n+loc_i];
						min_index = ai;
					}
				}
				uint32_t m_gw;
				char* m_ifname = (char*)malloc(sizeof(char)*SR_NAMELEN);
				
				if(min_index >= 0){
					int ret = findNeighbor(rtr_vec[min_index*n+curr_index[min_index]]->router_id, m_ifname, &m_gw);
					if(ret){
						int loc_i = get_index((topo_router**)&rtr_vec[min_index*n], n, rtr_vec_tot[i]->router_id); // source router
						dist_vec[min_index*n+loc_i] = INT_MAX;
						lsu_ad *nbr = rtr_vec[min_index*n+loc_i]->ads;
						while(nbr != NULL) {
						    //insert_shadow_node
						    insert_shadow_node(&shadow, nbr->subnet, nbr->mask, &m_gw, &m_ifname, 1, 0, fast_reroute_cnt);
						    nbr = nbr->next;
						}				
					}
				}			
				
				free(m_ifname);		
			} 
			fast_reroute_cnt++;
			if(subsystem->mode & 0x2){	// if fast reroute
				if(fast_reroute_cnt >= 2)
					break;
			}
			else{
				break;
			}
		}		

		free(curr_index);
    }

	pthread_mutex_unlock(&subsystem->mode_lock);

    //printf("Calling rebuild_rtable\n");
    rebuild_rtable(&(subsystem->rtable), shadow);

    // release all allocated memory
    free(adj_mat);
    free(rtr_vec);
    free(rtr_vec_tot);
    free(dist_vec);
    free(dist_vec_tot);
    free(parent_vec);
    free(tight_vec);

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
	head->last_update_time = (time_t)INT_MAX;
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
		while(nbr == NULL){ // no neighbors
			lsu_ad *node = (lsu_ad*)malloc(sizeof(lsu_ad));
			node->subnet = pif->ip & pif->netmask;
			node->mask = pif->netmask;
			node->router_id = 0;
			node->next = NULL;
			node->prev = NULL;
				
			advCnt++;
						
			if(head->ads == NULL){
				head->ads = node;
				break;
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
			break;
		}
		while(nbr){
			lsu_ad *node = (lsu_ad*)malloc(sizeof(lsu_ad));
			node->subnet = nbr->ip & nbr->nm;
			node->mask = nbr->nm;
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
