#include "sr_vns.h"
#include "sr_base_internal.h"
#include "sr_integration.h"
#include "arpCache.h"

#define ETHERNET_HEADER_LENGTH 14
#define ARP_HEADER_LENGTH 8
#define IP_HEADER_LENGTH 20

#define ARP_CACHE_REFRESH 20

struct sr_router{
	arpNode *arpList;
	arpTreeNode *arpTree;
	int num_ifaces;
	struct sr_vns_if* ifaces;
};

void processPacket(struct sr_instance* sr,
        const uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */);
        

uint8_t* getMAC(struct sr_instance* sr, uint32_t ip, const char* name);
uint8_t* generateARPreply(const uint8_t *packet, size_t len, uint8_t *mac);
void sendARPrequest(struct sr_instance* sr, const char* interface, uint32_t ip);

void* arpCacheRefresh(void *dummy);

void testList(struct sr_instance* sr);
