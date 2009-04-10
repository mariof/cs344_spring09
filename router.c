#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "router.h"

enum packetType {IPv4, ARP};

inline void errorMsg(char* msg){
	fputs("error: ", stderr); fputs(msg, stderr); fputs("\n", stderr);
}

inline void dbgMsg(char* msg){
	fputs("dbg: ", stdout); fputs(msg, stdout); fputs("\n", stdout);
}

// this function processes all input packets
void processPacket(struct sr_instance* sr,
        const uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */)
{
    int i;
        
    if (len < ETHERNET_HEADER_LENGTH){
    	errorMsg("Ethernet Packet too short");
    	return;
    }
    
    // see if input packet is IPv4 or ARP
    if (packet[12] == 8 && packet[13] == 0){
        dbgMsg("IPv4 packet received");
    
    }
    else if (packet[12] == 8 && packet[13] == 6){
	    if (len < ETHERNET_HEADER_LENGTH + ARP_HEADER_LENGTH){
	    	errorMsg("ARP Packet too short");
	    	return;
	    }
    	const uint8_t* arpPacket = &packet[ETHERNET_HEADER_LENGTH];
    	    	
    	// handle ARP requests and responses    	
    	if (arpPacket[6] == 0 && arpPacket[7] == 1){
    		dbgMsg("ARP request received");
			size_t ipLen = arpPacket[5];
			size_t macLen = arpPacket[4];
    		const uint8_t* arpPacketData = &arpPacket[ARP_HEADER_LENGTH];
    		uint32_t dstIP;
    		
    		dstIP = arpPacketData[macLen + ipLen + macLen + 3] * 1 + 
    				arpPacketData[macLen + ipLen + macLen + 2] * 256 +
    				arpPacketData[macLen + ipLen + macLen + 1] * 256 * 256 +
    				arpPacketData[macLen + ipLen + macLen + 0] * 256 * 256 * 256;
    
    	    //for(i = macLen+ipLen+macLen; i < macLen+ipLen+macLen+4; i++) printf("%d: %d\n", i, arpPacketData[i]);
			uint8_t* if_mac = getMAC(sr, ntohl(dstIP), interface);
				
			if (if_mac){
				dbgMsg("IP match found, need to send ARP response");				
				uint8_t* arpReply = generateARPreply(packet, len, if_mac);
				sr_integ_low_level_output(sr, arpReply, 60, interface);
				free(arpReply);
			}
    	
    	}
    	else if (arpPacket[6] == 0 && arpPacket[7] == 2){
    		dbgMsg("ARP response received");
    	}
    	    
    }
     	       
}

// get interface's MAC address if given correct name and IP address
uint8_t* getMAC(struct sr_instance* sr, uint32_t ip, const char* name){

	int i;
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// find the interface by name, and then check IP
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (strcmp(name, subsystem->ifaces[i].name))
			continue;
			
		if (ip == subsystem->ifaces[i].ip){ // both should be in host byte order
			return subsystem->ifaces[i].addr;
		}	
	}	
	return NULL;
}


// returns a 60 byte ARP reply packet from ARP request packet
uint8_t* generateARPreply(const uint8_t *packet, size_t len, uint8_t *mac){
	int i, j;
	uint8_t *p = (uint8_t*)malloc(60*sizeof(uint8_t));

	if (len < 60){
		errorMsg("Received packet too small");
		return NULL;
	}

	// generate Ethernet Header
	for (i = 0, j = 6; i < 6; i++, j++) p[i] = packet[j];
	for (i = 6, j = 0; j < 12; i++, j++) p[i] = mac[j];	
	p[12] = 8; p[13] = 6;
	
	// generate ARP Header
	for (i = 14; i < 20; i++) p[i] = packet[i];
	p[20] = 0; p[21] = 2;
		
	// generate ARP data
	for (i = 22, j = 0; i < 28; i++, j++) p[i] = mac[j];
	for (i = 28, j = 38; i < 32; i++, j++) p[i] = packet[j];
	for (i = 32, j = 22; i < 42; i++, j++) p[i] = packet[j];
	for (i = 42; i < 60; i++) p[i] = 0;

	
	return p;
} 


void testList(struct sr_instance* sr){
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
 	uint32_t ip1 = 657;
 	uint8_t mac1[6] = {12,32,11,99,55,22};
 	uint32_t ip2 = 267;
 	uint8_t mac2[6] = {2,122,13,91,35,42};
 	uint32_t ip3 = 435;
 	uint8_t mac3[6] = {112,52,101,9,5,32};
 	 	
 	insert(&subsystem->arpList, ip1, mac1);
	insert(&subsystem->arpList, ip2, mac2);
	insert(&subsystem->arpList, ip3, mac3);

	arpNode *cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}
	
	
	printf("\n");
	deleteMAC(&subsystem->arpList, mac2);
	deleteIP(&subsystem->arpList, ip3);
	insert(&subsystem->arpList, ip1, mac1);
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}


	sleep(2);
	insert(&subsystem->arpList, ip2, mac2);
	sleep(2);
	insert(&subsystem->arpList, ip3, mac3);

	printf("\n");
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}

	timeout(&subsystem->arpList);

	printf("\n");
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}

	
	subsystem->arpTree = generateTree(subsystem->arpList);
	printf("\n");
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}


	uint8_t *m = lookupTree(subsystem->arpTree, ip3);
	printf("ip: %u matches: %u:%u...\n", ip3, m[0], m[1]);
	free(m);

	destroyTree(subsystem->arpTree);
}
