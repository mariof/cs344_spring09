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
	uint32_t gateway;
	char output_if[SR_NAMELEN];
	time_t t;
	struct routingTableNode *prev;
	struct routingTableNode *next;
};

typedef struct routingTableNode rtableNode;

void insert_rtable_node(rtableNode **head, uint32_t ip, uint32_t netmask, uint32_t gateway, const char* output_if);
void del_ip(rtableNode **head, uint32_t ip, uint8_t netmask);
char *lp_match(rtableNode **head, uint32_t ip);
uint32_t gw_match(rtableNode **head, uint32_t ip);

pthread_mutex_t rtable_lock;
