#ifndef ROUTER_H
#define ROUTER_H

#include <stdlib.h>
#include "sr_vns.h"
#include "sr_base_internal.h"
#include "sr_integration.h"
#include <pthread.h>
#include "arpCache.h"
#include "arpQueue.h"
#include "icmpMsg.h"
#include "routingTable.h"
#include "threadPool.h"
#include "pwospf.h"
#include "topology.h"

#ifdef _CPUMODE_

#include "nf2util.h"
#include "nf2.h"
#include "reg_defines_cs344_starter.h"

extern struct nf2device netFPGA;

pthread_mutex_t ifRegLock;
pthread_mutex_t filtRegLock;
pthread_mutex_t arpRegLock;
pthread_mutex_t routeRegLock;

#endif // _CPUMODE_

#define ETHERNET_HEADER_LENGTH 14
#define ARP_HEADER_LENGTH 8
#define IP_HEADER_LENGTH 20
#define ICMP_HEADER_LENGTH 4
#define OSPF_HEADER_LENGTH 24

#define ARP_CACHE_REFRESH 1
#define ARP_QUEUE_REFRESH 1
#define PING_LIST_REFRESH 1
#define TOPO_REFRESH 1


struct sr_router{
	struct arpQueueNode* arpQueue;
	arpNode *arpList;
	arpTreeNode *arpTree;
	rtableNode *rtable;
	int num_ifaces;
	pthread_mutex_t mode_lock;
	int mode; // 0 - normal; 1 - multipath; 2 - fast reroute; 3 - both
	struct sr_vns_if* ifaces;
	pthread_rwlock_t if_lock;
	struct threadWorker* poolHead;
	struct threadWorker* poolTail;
	struct pwospf_router pwospf;
};

void processPacket(struct sr_instance* sr,
        uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */);
        
inline void errorMsg(char* msg);
inline void dbgMsg(char* msg);

uint8_t* getMAC(struct sr_instance* sr, uint32_t ip, const char* name);
uint8_t* generateARPreply(const uint8_t *packet, size_t len, uint8_t *mac);
void sendARPrequest(struct sr_instance* sr, const char* interface, uint32_t ip);
void sendIPpacket(struct sr_instance* sr, const char* interface, uint32_t ip, uint8_t* packet, unsigned len);
int isMyIP(uint32_t ip);
int isEnabled(uint32_t ip);
char* getIfName(uint32_t ip);
char* getIfNameFromMAC(uint8_t *mac);
void writeARPCache(arpTreeNode *node, int *index);
void writeRoutingTable();
int setMultipath(int multipath);
int setFastReroute(int fast);
int getMode();

void int2byteIP(uint32_t ip, uint8_t *byteIP);
uint32_t getInterfaceIP(const char* interface);
uint32_t getNextHopIP(uint32_t ip);
void printARPCache();

void arpCacheRefresh(void *dummy);
void topologyRefresh(void *dummy);

void testList(struct sr_instance* sr);

void fill_rtable(rtableNode **head);

void sr_transport_input(uint8_t* packet /* borrowed */);

#ifdef _CPUMODE_

void writeIPfilter();

#endif // _CPUMODE_

/**
 * ---------------------------------------------------------------------------
 * -------------------- CLI Functions ----------------------------------------
 * ---------------------------------------------------------------------------
 */

int router_interface_set_enabled( struct sr_instance* sr, const char* name, int enabled );
struct sr_vns_if* router_lookup_interface_via_ip( struct sr_instance* sr, uint32_t ip );
struct sr_vns_if* router_lookup_interface_via_name( struct sr_instance* sr, const char* name );
int router_is_interface_enabled( struct sr_instance* sr, void* intf );


#endif // ROUTER_H
