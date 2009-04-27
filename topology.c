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
