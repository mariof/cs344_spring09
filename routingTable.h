#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include "sr_vns.h"
#include "sr_base_internal.h"
#include "sr_integration.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* the routing table is maintained as an ordered linked list
 * sorted by the decreasing order of
 * - netmask
 * - IP address
 */

struct routingTableNode {
	uint32_t ip;
	uint32_t netmask;
	uint32_t* gateway;
	char** output_if;
	int out_cnt;
	int is_static;
	time_t t;
	struct routingTableNode *prev;
	struct routingTableNode *next;
};

typedef struct routingTableNode rtableNode;
pthread_mutex_t rtable_lock;

void insert_rtable_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t* gateway, char** output_if, int out_cnt, int is_static);
void merge_rtable_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t* gateway, char** output_if, int out_cnt, int is_static);
int del_ip(rtableNode **head, uint32_t ip, uint32_t netmask, int is_static);
void del_route_type(rtableNode **head, int is_static);
char *lp_match(rtableNode **head, uint32_t ip);
uint32_t gw_match(rtableNode **head, uint32_t ip);
/* Replace all the dynamic routing table entries
 * with those from the shadow table
 */
void rebuild_rtable(rtableNode **head, rtableNode *shadow_table);

/**
 * ---------------------------------------------------------------------------
 * -------------------- CLI Functions ----------------------------------------
 * ---------------------------------------------------------------------------
 */
void rtable_route_add( struct sr_instance* sr,
                       uint32_t dest, uint32_t gw, uint32_t mask,
                       void* intf,
                       int is_static_route );
void rtable_route_addm( struct sr_instance* sr,
                       uint32_t dest, uint32_t gw, uint32_t mask,
                       void* intf,
                       int is_static_route );
int rtable_route_remove( struct sr_instance* sr,
                         uint32_t dest, uint32_t mask,
                         int is_static );
void rtable_purge_all( struct sr_instance* sr );
void rtable_purge( struct sr_instance* sr, int is_static );

#endif // ROUTING_TABLE_H
