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

void inorderPrintTree(arpTreeNode *node);

// this function processes all input packets
void processPacket(struct sr_instance* sr,
        const uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */)
{
    int i;
   	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
        
    if (len < ETHERNET_HEADER_LENGTH){
    	errorMsg("Ethernet Packet too short");
    	return;
    }
    
    // see if input packet is IPv4 or ARP
    if (packet[12] == 8 && packet[13] == 0){ // IPv4
        dbgMsg("IPv4 packet received");
	    if (len < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH){
	    	errorMsg("IP Packet too short");
	    	return;
	    }
    	const uint8_t* ipPacket = &packet[ETHERNET_HEADER_LENGTH];

    	uint32_t testIP, dstIP;
    		
    	testIP =	172 * 256 * 256 * 256 +
   				 	24 * 256 * 256 +
    			 	74 * 256 +
				 	11 * 1; 
   				
    	dstIP = ipPacket[16] * 256 * 256 * 256 +
   				ipPacket[17] * 256 * 256 +
    			ipPacket[18] * 256 +
				ipPacket[19] * 1; 
   				
		testIP = ntohl(testIP);
		dstIP = ntohl(dstIP);

		// find the interface by name
		for(i = 0; i < subsystem->num_ifaces; i++){
			if (!strcmp(interface, subsystem->ifaces[i].name))
				break;
		}
		if (i >= subsystem->num_ifaces){
			errorMsg("Given interfaces does not exist");
			return;
		}		
		uint32_t myIP = subsystem->ifaces[i].ip; // host byte order


		if(myIP == dstIP){
			dbgMsg("Received packet destined for the router");
			//sr_transport_input(packet);
		}
		else{
			dbgMsg("Forwarding received packet");
		}		
		
		// just testing
		sendIPpacket(sr, interface, testIP, (uint8_t*)packet, len);
    
    }
    else if (packet[12] == 8 && packet[13] == 6){ // ARP
	    if (len < ETHERNET_HEADER_LENGTH + ARP_HEADER_LENGTH){
	    	errorMsg("ARP Packet too short");
	    	return;
	    }
    	const uint8_t* arpPacket = &packet[ETHERNET_HEADER_LENGTH];
 
 		//for(i = 0; i < len; i++) printf("%d: %d\n", i, packet[i]);
    	    	
    	// handle ARP requests and responses    	
    	if (arpPacket[6] == 0 && arpPacket[7] == 1){
    		dbgMsg("ARP request received");
			size_t ipLen = arpPacket[5];
			size_t macLen = arpPacket[4];
    		const uint8_t* arpPacketData = &arpPacket[ARP_HEADER_LENGTH];
    		uint32_t dstIP;
    		
    		dstIP = arpPacketData[macLen + ipLen + macLen + 0] * 256 * 256 * 256 +
    				arpPacketData[macLen + ipLen + macLen + 1] * 256 * 256 +
    				arpPacketData[macLen + ipLen + macLen + 2] * 256 +
    				arpPacketData[macLen + ipLen + macLen + 3] * 1; 
    				
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
			size_t macLen = arpPacket[4];
    		const uint8_t* arpPacketData = &arpPacket[ARP_HEADER_LENGTH];
    		uint32_t srcIP;
    		uint8_t srcMAC[6];
    		
    		for(i = 0; i < macLen; i++) srcMAC[i] = arpPacketData[i];
    		
    		srcIP = arpPacketData[macLen + 0] * 256 * 256 * 256 +
    				arpPacketData[macLen + 1] * 256 * 256 +
    				arpPacketData[macLen + 2] * 256 +
    				arpPacketData[macLen + 3] * 1;  				
    		srcIP = ntohl(srcIP); 
    	
    		arpInsert(&subsystem->arpList, srcIP, srcMAC, 0);
			arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));
			//inorderPrintTree(subsystem->arpTree);
			
			// send queues
			queueSend(srcIP, interface);
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
	for (i = 6, j = 0; i < 12; i++, j++) p[i] = mac[j];	
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

// converts IP from host byte order integer to network byte order byte array
void int2byteIP(uint32_t ip, uint8_t *byteIP){
	uint32_t nip = htonl(ip);
	
	byteIP[3] = (uint8_t)(nip % 256);
	nip = nip / 256;
	byteIP[2] = (uint8_t)(nip % 256);
	nip = nip / 256;
	byteIP[1] = (uint8_t)(nip % 256);
	nip = nip / 256;
	byteIP[0] = (uint8_t)(nip % 256);	
}

// returns a 60 byte ARP request packet, use sendARPrequest instead
uint8_t* generateARPrequest(struct sr_instance* sr, const char* interface, uint32_t ip){
	int i, j;
	uint8_t *p = (uint8_t*)malloc(60*sizeof(uint8_t));
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// find the interface by name
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(interface, subsystem->ifaces[i].name))
			break;
	}
	if (i >= subsystem->num_ifaces){
		errorMsg("Given interfaces does not exist");
		return NULL;
	}	
	
	uint8_t *myMAC = subsystem->ifaces[i].addr;
	uint32_t myIP = subsystem->ifaces[i].ip; // host byte order

	// generate Ethernet Header
	for (i = 0, j = 6; i < 6; i++, j++) p[i] = 255;
	for (i = 6, j = 0; i < 12; i++, j++) p[i] = myMAC[j];	
	p[12] = 8; p[13] = 6;
	
	// generate ARP Header
	p[14] = 0; p[15] = 1; p[16] = 8; p[17] = 0; p[18] = 6; p[19] = 4;
	p[20] = 0; p[21] = 1;
		
	// generate ARP data
	for (i = 22, j = 0; i < 28; i++, j++) p[i] = myMAC[j];
	int2byteIP(myIP, &p[28]);
	for (i = 32; i < 38; i++) p[i] = 0;
	int2byteIP(ip, &p[38]);
	for (i = 42; i < 60; i++) p[i] = 0;
	
	return p;
} 

// sends ARP request for ip (host byte order)
void sendARPrequest(struct sr_instance* sr, const char* interface, uint32_t ip){
	uint8_t *arprq = generateARPrequest(sr, interface, ip);
	//for(i = 0; i < 60; i++) printf("::%d: %d\n", i, arprq[i]);				
	sr_integ_low_level_output(sr, arprq, 60, interface);
	free(arprq);			
}

// runs every ~20 seconds in a separate thread to see if any arp cache entries have timed out
void* arpCacheRefresh(void *dummy){
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	while(1){
		if( arpTimeout(&subsystem->arpList) )
			arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));
		sleep(ARP_CACHE_REFRESH);
	}
}


// Sends out packet to address ip out the "interface". Packet has to have a placeholder for Ethernet header. Packet is just borrowed (not destroyed here)
void sendIPpacket(struct sr_instance* sr, const char* interface, uint32_t ip, uint8_t* packet, unsigned len){
	int i,j;
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// find the interface by name
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(interface, subsystem->ifaces[i].name))
			break;
	}
	if (i >= subsystem->num_ifaces){
		errorMsg("Given interfaces does not exist");
		return;
	}		
	uint8_t *myMAC = subsystem->ifaces[i].addr;

	// fill Ethernet Header
	for (i = 0; i < 6; i++) packet[i] = 0;
	for (i = 6, j = 0; i < 12; i++, j++) packet[i] = myMAC[j];	
	packet[12] = 8; packet[13] = 0;

	// get destination MAC
	uint8_t *dstMAC = arpLookupTree(subsystem->arpTree, ip);
	
	if(dstMAC){
		dbgMsg("Sending packet");
		for (i = 0; i < 6; i++) packet[i] = dstMAC[i];
		sr_integ_low_level_output(sr, packet, len, interface);	
		free(dstMAC);	
	}
	else{ // send out ARP and queue the packet
		dbgMsg("Queueing packet");
		sendARPrequest(sr, interface, ip);
		queuePacket(packet, len, interface, ip);
	}	
}



//////////////////////////////
// test functions
//////////////////////////////

void testList(struct sr_instance* sr){
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
 	uint32_t ip1 = 657;
 	uint8_t mac1[6] = {12,32,11,99,55,22};
 	uint32_t ip2 = 267;
 	uint8_t mac2[6] = {2,122,13,91,35,42};
 	uint32_t ip3 = 435;
 	uint8_t mac3[6] = {112,52,101,9,5,32};
 	 	
 	arpInsert(&subsystem->arpList, ip1, mac1, 1);
	arpInsert(&subsystem->arpList, ip2, mac2, 1);
	arpInsert(&subsystem->arpList, ip3, mac3, 1);

	arpNode *cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}
	
	
	printf("\n");
	arpDeleteMAC(&subsystem->arpList, mac2);
	arpDeleteIP(&subsystem->arpList, ip3);
	arpInsert(&subsystem->arpList, ip1, mac1, 0);
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}

	sleep(0);
	arpInsert(&subsystem->arpList, ip2, mac2, 0);
	sleep(0);
	arpInsert(&subsystem->arpList, ip3, mac3, 1);

	printf("\n");
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}

	arpTimeout(&subsystem->arpList);

	printf("\n");
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}

	
	subsystem->arpTree = arpGenerateTree(subsystem->arpList);
	printf("\n");
	cur = subsystem->arpList;
	while(cur){
		printf("ip: %u, mac: %u:%u:%u...\n", cur->ip, cur->mac[0], cur->mac[1], cur->mac[2]);
		cur = cur->next;
	}


	uint8_t *m = arpLookupTree(subsystem->arpTree, ip3);
	printf("ip: %u matches: %u:%u...\n", ip3, m[0], m[1]);
	free(m);

	arpReplaceTree(&subsystem->arpTree, NULL);
}

void inorderPrintTree(arpTreeNode *node){
	if(node == NULL) return;
	if(node->left) inorderPrintTree(node->left);
	printf("ip: %u, mac:%d:%d:...\n", node->ip, node->mac[0], node->mac[1]);
	if(node->right) inorderPrintTree(node->right);
}