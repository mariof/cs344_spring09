#include "sr_vns.h"
#include "sr_base_internal.h"
#include "sr_integration.h"

#define ETHERNET_HEADER_LENGTH 14
#define ARP_HEADER_LENGTH 8

struct sr_router{
	int test;
	int num_ifaces;
	struct sr_vns_if* ifaces;
};

void processPacket(struct sr_instance* sr,
        const uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */);
        

uint8_t* getMAC(struct sr_instance* sr, uint32_t ip, const char* name);
uint8_t* generateARPreply(const uint8_t *packet, size_t len, uint8_t *mac);
