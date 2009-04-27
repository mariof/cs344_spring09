#ifndef TOPOLOGY_H
#define TOPOLOGY_H

#include "routingTable.h"

struct lsu_ad {
    uint32_t subnet;
    uint32_t mask;
    uint32_t router_id;

    struct lsu_ad *next;
    struct lsu_ad *prev;
};

struct topo_router {
    uint32_t router_id;
    uint32_t area_id;
    uint16_t last_seq;
    time_t last_update_time;
    uint32_t num_ads;

    struct lsu_ad *ads;

    struct topo_router *next;
    struct topo_router *prev;
};

pthread_mutex_t topo_lock;
struct topo_router *topo_head;

int add_router(uint32_t router_id, uint16_t last_seq);
int rm_router(uint32_t router_id);
int add_router_ad(uint32_t router_id, uint32_t subnet, uint32_t mask, uint32_t nbr_router_id);

#endif
