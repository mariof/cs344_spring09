#include "sr_vns.h"

#define ETHERNET_HEADER_LENGTH 14
#define ARP_HEADER_LENGTH 8

struct sr_router{
	int test;
};

void processPacket(struct sr_instance* sr,
        const uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */);
