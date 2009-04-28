#ifndef TOPOLOGY_H
#define TOPOLOGY_H

/*
 * Topology database
 */

#include "routingTable.h"

typedef struct nbr {
    uint32_t subnet;
    uint32_t mask;
    uint32_t router_id;

    struct nbr *next;
    struct nbr *prev;
} lsu_ad;

typedef struct topology_router {
    uint32_t router_id;
    uint32_t area_id;
    uint16_t last_seq;
    time_t last_update_time;
    uint32_t num_ads;

    lsu_ad *ads;

    struct topology_router *next;
    struct topology_router *prev;
} topo_router;

pthread_mutex_t topo_lock;
topo_router *topo_head;
int num_routers;

/*
 * returns:
 ** 1 if the topology needed an update
 ** 0 otherwise
 */
int add_router(uint32_t router_id, uint16_t seq);
/* 
 * returns -1 if router_id doesn't exist in the topology
 */
int get_last_seq(uint32_t router_id);
/*
 * returns:
 ** 1 if the topology needed an update
 ** 0 otherwise
 */
int rm_router(uint32_t router_id);
/*
 * returns:
 ** 1 if the topology needed an update
 ** 0 if topology needed no update
 ** -1 if router_id wasn't found
 */
int add_router_ad(uint32_t router_id, uint32_t subnet, uint32_t mask, uint32_t nbr_router_id);

/* takes an adjacency list of nodes
 ** note: the nodes in the list must be sorted in increasing order of router_id
 * returns:
 ** 1 if the topology needed an update
 ** 0 otherwise
 */
int update_lsu(topo_router *adj_list); 
void update_rtable();

#endif
