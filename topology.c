#include "topology.h"

int add_router(uint32_t router_id, uint16_t last_seq)
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);
    //release lock
    pthread_mutex_unlock(&topo_lock);
    return -1;
}

int rm_router(uint32_t router_id)
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);
    //release lock
    pthread_mutex_unlock(&topo_lock);
    return -1;
}

int add_router_ad(uint32_t router_id, uint32_t subnet, uint32_t mask, uint32_t nbr_router_id)
{
    //acquire lock
    pthread_mutex_lock(&topo_lock);
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

void update_rtable()
{
    int n = num_routers;
    //malloc matrix
    //[i][j] = [i*n+j]
    int *adj_mat = malloc(sizeof(int)*n*n);
}
