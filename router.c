#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "router.h"

#ifdef _CPUMODE_
struct nf2device netFPGA;
#endif // _CPUMODE_

inline void errorMsg(char* msg){
	fputs("error: ", stderr); fputs(msg, stderr); fputs("\n", stderr);
}

inline void dbgMsg(char* msg){
	fputs("dbg: ", stdout); fputs(msg, stdout); fputs("\n", stdout);
}

void inorderPrintTree(arpTreeNode *node);

// this function processes all input packets
void processPacket(struct sr_instance* sr,
        uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */)
{
    int i;
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int iface_disabled = 0;
    
	pthread_rwlock_rdlock(&subsystem->if_lock);	
    for(i = 0; i < subsystem->num_ifaces; i++) {
		if(!strcmp(subsystem->ifaces[i].name, interface)) {
		    if(!(subsystem->ifaces[i].enabled)) {
			iface_disabled = 1;
		    }
		    break;
		}
    }
    pthread_rwlock_unlock(&subsystem->if_lock);

    if(iface_disabled) {
		return;
    }
        
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
    	uint8_t* ipPacket = &packet[ETHERNET_HEADER_LENGTH];

	// check checksum
	uint16_t word16, sum16;
	uint32_t sum = 0;
	uint16_t i;
	uint8_t header_len = (ipPacket[0] & 0x0F)*4; // no. of bytes
	    
	// make 16 bit words out of every two adjacent 8 bit words in the packet
	// and add them up
	for (i = 0; i < header_len; i+=2) {
	    word16 = ipPacket[i] & 0xFF;
	    word16 = (word16 << 8) + (ipPacket[i+1] & 0xFF);
	    sum += (uint32_t)word16;	
	}

	// take only 16 bits out of the 32 bit sum and add up the carries
	while (sum >> 16)
	    sum = (sum & 0xFFFF) + (sum >> 16);
	sum16 = ~((uint16_t)(sum & 0xFFFF));

	if(sum16 != 0) {
	    /* checksum error
	     * drop packet
	     */
	    errorMsg("Checksum error! Dropping the packet.");
	    return;
	}
	
	uint8_t ttl = ipPacket[8];

	// check TTL
	if(ttl < 1) {
	    /* drop packet
	     * send icmp packet back to the source
	     */
	    errorMsg("TTL went to 0. Dropping packet");
	    sendICMPTimeExceeded(interface, packet, len);
	    return;
	}
	
    uint32_t nextHopIP, dstIP;
	char *out_if = NULL;
    		
	/*uint32_t testIP;
	testIP =	172 * 256 * 256 * 256 +
	    24 * 256 * 256 +
	    74 * 256 +
	    11 * 1; 
	testIP = ntohl(testIP);*/

	dstIP = ipPacket[16] * 256 * 256 * 256 +
	    ipPacket[17] * 256 * 256 +
	    ipPacket[18] * 256 +
	    ipPacket[19] * 1; 

	// find the interface with target IP
	uint32_t myIP = 0;
	int is_broadcast = 0;
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		myIP = subsystem->ifaces[i].ip; // host byte order
		if(myIP == dstIP) break;
		if(( myIP | (~subsystem->ifaces[i].mask) ) == dstIP){ // broadcast IP address
			is_broadcast = 1;
			break;
		}
	}
	pthread_rwlock_unlock(&subsystem->if_lock);

	if(myIP == dstIP){
	    dbgMsg("Received packet destined for the router");
	    if(ipPacket[9] == 1){ // ICMP
			processICMP(interface, packet, len);
	    }
	    else if(ipPacket[9] == 6){ // TCP
			sr_transport_input(ipPacket);
	    } 
	    else if(ipPacket[9] == 89){ // OSPF
			processPWOSPF(interface, packet, len);
	    } 
	    else{ // protocol not supported
			dbgMsg("Transport Protocol not supported");
			sendICMPDestinationUnreachable(interface, packet, len, 2);
	    }
	}
	else if (is_broadcast){ // broadcast IP
		// nothing to do really, ignoring all packets
	}
	else if (dstIP == ALLSPFRouters){
	    if(ipPacket[9] == 89){ // OSPF
			processPWOSPF(interface, packet, len);
	    } 		
	} 
	
	else{
		ttl = ipPacket[8];

		nextHopIP = gw_match(&(subsystem->rtable), dstIP); //nextHopIP in hbo
		if(!nextHopIP) {
		    errorMsg("Destination network unreachable. Dropping packet");
		    sendICMPDestinationUnreachable(interface, packet, len, 0);
		    return;
		}

		out_if = lp_match(&(subsystem->rtable), dstIP); //output interface
		if(!out_if) {
		    errorMsg("Destination network unreachable. Dropping packet");
		    sendICMPDestinationUnreachable(interface, packet, len, 0);
		    return;
		}

		// check TTL
		if(ttl <= 1) {
		    /* drop packet
		     * send icmp packet back to the source
		     */
		    errorMsg("TTL went to 0. Dropping packet");
		    sendICMPTimeExceeded(interface, packet, len);
		    return;
		}

		// decrement TTL
		ttl--;
		ipPacket[8] = ttl;

		// update checksum
		uint32_t csum = ipPacket[10] & 0xFF;
		csum = (csum << 8) + (ipPacket[11] & 0xFF);
		csum += 0x100;
		csum = ((csum >> 16) + csum) & 0xFFFF;
		ipPacket[10] = (csum >> 8) & 0xFF;
		ipPacket[11] = csum & 0xFF;
		
	    dbgMsg("Forwarding received packet");
//	    printf("from: %u.%u.%u.%u\n", ipPacket[12], ipPacket[13], ipPacket[14], ipPacket[15]);
//	    printf("to: %u.%u.%u.%u\n", ipPacket[16], ipPacket[17], ipPacket[18], ipPacket[19]);
	    sendIPpacket(sr, out_if, nextHopIP, (uint8_t*)packet, len);
	}		

	free(out_if);

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
    	//	dbgMsg("ARP request received");
			size_t ipLen = arpPacket[5];
			size_t macLen = arpPacket[4];
    		const uint8_t* arpPacketData = &arpPacket[ARP_HEADER_LENGTH];
    		uint32_t dstIP;
    		
    		dstIP = arpPacketData[macLen + ipLen + macLen + 0] * 256 * 256 * 256 +
    				arpPacketData[macLen + ipLen + macLen + 1] * 256 * 256 +
    				arpPacketData[macLen + ipLen + macLen + 2] * 256 +
    				arpPacketData[macLen + ipLen + macLen + 3] * 1; 
    				
    	    //for(i = macLen+ipLen+macLen; i < macLen+ipLen+macLen+4; i++) printf("%d: %d\n", i, arpPacketData[i]);
			uint8_t* if_mac = getMAC(sr, dstIP, interface);
				
			if (if_mac){
				dbgMsg("IP match found, need to send ARP response");				
				uint8_t* arpReply = generateARPreply(packet, len, if_mac);
				if(arpReply) {
				    sr_integ_low_level_output(sr, arpReply, 60, interface);
				    free(arpReply);
				}
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
    	
    		arpInsert(&subsystem->arpList, srcIP, srcMAC, 0);
			arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));
			
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
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (strcmp(name, subsystem->ifaces[i].name))
			continue;
			
		if (ip == subsystem->ifaces[i].ip){ // both should be in host byte order
			pthread_rwlock_unlock(&subsystem->if_lock);
			return subsystem->ifaces[i].addr;
		}	
	}	
	pthread_rwlock_unlock(&subsystem->if_lock);
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

	byteIP[3] = (uint8_t)(ip % 256);
	ip = ip / 256;
	byteIP[2] = (uint8_t)(ip % 256);
	ip = ip / 256;
	byteIP[1] = (uint8_t)(ip % 256);
	ip = ip / 256;
	byteIP[0] = (uint8_t)(ip % 256);	
}

// returns interface IP
uint32_t getInterfaceIP(const char* interface){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	uint32_t retVal = 0;
	
	// find the interface by name
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(interface, subsystem->ifaces[i].name)){
			retVal = subsystem->ifaces[i].ip;
			break;
		}
	}
	pthread_rwlock_unlock(&subsystem->if_lock);
	return retVal;
}

// returns 1 if given IP is one of the router's interfaces
int isMyIP(uint32_t ip){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	uint32_t retVal = 0;
	
	// loop interfaces
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (subsystem->ifaces[i].ip == ip){
			retVal = 1;
			break;
		}
	}
	pthread_rwlock_unlock(&subsystem->if_lock);
	return retVal;
}

// returns interface name given IP. Pointer is borrowed from the original interface structure
char* getIfName(uint32_t ip){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	
	// loop interfaces
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
	    //printf("getIfName: for-loop %d\n", i);
		if (subsystem->ifaces[i].ip == ip){
		    //printf("getIfName: found the ip\n");
		    char *ifname = subsystem->ifaces[i].name;
		    //printf("Interface name = %s\n", ifname);
			pthread_rwlock_unlock(&subsystem->if_lock);
			return ifname;
		}
	}
	pthread_rwlock_unlock(&subsystem->if_lock);
	return NULL;
}

// returns interface name given MAC address. Pointer is borrowed from the original interface structure
char* getIfNameFromMAC(uint8_t *mac){
	int i, j;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	
	// loop interfaces
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		int match = 1;
		for(j = 0; j < 6; j++){
			if(subsystem->ifaces[i].addr[j] != mac[j]){
				match = 0;
				break;
			}
		}
		if (match){
			pthread_rwlock_unlock(&subsystem->if_lock);
			return subsystem->ifaces[i].name;
		}
	}
	pthread_rwlock_unlock(&subsystem->if_lock);
	return NULL;
}

// returns 1 if the interface with given IP is enabled
int isEnabled(uint32_t ip){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	int retVal = 0;
	
	// loop interfaces
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (subsystem->ifaces[i].ip == ip){
			retVal = subsystem->ifaces[i].enabled;
			break;
		}
	}
	pthread_rwlock_unlock(&subsystem->if_lock);
	return retVal;
}

// returns a 60 byte ARP request packet, use sendARPrequest instead
uint8_t* generateARPrequest(struct sr_instance* sr, const char* interface, uint32_t ip){
	int i, j;
	uint8_t *p = (uint8_t*)malloc(60*sizeof(uint8_t));
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// find the interface by name
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(interface, subsystem->ifaces[i].name))
			break;
	}
	pthread_rwlock_unlock(&subsystem->if_lock);
	if (i >= subsystem->num_ifaces){
		errorMsg("Given interfaces does not exist");
		free(p);
		return NULL;
	}	
	
	pthread_rwlock_rdlock(&subsystem->if_lock);
	uint8_t *myMAC = subsystem->ifaces[i].addr;
	uint32_t myIP = subsystem->ifaces[i].ip; // host byte order
	pthread_rwlock_unlock(&subsystem->if_lock);

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
	//int i;
	uint8_t *arprq = generateARPrequest(sr, interface, ip);
	//for(i = 0; i < 60; i++) printf("::%d: %d\n", i, arprq[i]);				
	sr_integ_low_level_output(sr, arprq, 60, interface);
	free(arprq);			
}

// runs every ~2 seconds in a separate thread to see if any arp cache entries have timed out
void arpCacheRefresh(void *dummy){
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	while(1){
		if( arpTimeout(&subsystem->arpList) )
			arpReplaceTree(&subsystem->arpTree, arpGenerateTree(subsystem->arpList));
		sleep(ARP_CACHE_REFRESH);
	}
}

// runs every ~2 seconds in a separate thread to see if any topology node entries have timed out
void topologyRefresh(void *dummy){
    while(1) {
	if(purge_topo()) {
	    update_rtable();
	}
	sleep(TOPO_REFRESH);
    }
}

// given destination IP, returns next hop ip
uint32_t getNextHopIP(uint32_t ip){
	uint32_t retVal;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	dbgMsg("Looking up IP in routing table");
	retVal = gw_match(&subsystem->rtable, ip);
	
//	uint8_t si[4], di[4];
//	int2byteIP(ip, si);
//	int2byteIP(retVal, di);
	
	//printf("from: %u.%u.%u.%u to: %u.%u.%u.%u\n", si[0], si[1], si[2], si[3], di[0], di[1], di[2], di[3]);
	return retVal;
}

// Sends out packet to next hop ip address "ip" out the "interface". Packet has to have a placeholder for Ethernet header. Packet is just borrowed (not destroyed here)
// interface parameter is ignored, output if is calculated from the IP
void sendIPpacket(struct sr_instance* sr, const char* interface, uint32_t ip, uint8_t* packet, unsigned len){
	int i,j;
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	if(isMyIP(ip)){
		dbgMsg("Cannot send to myself!");
		return;
	}

	char * out_if = lp_match(&(subsystem->rtable), ip); // make sure output interface is correct

	if (out_if == NULL){
		dbgMsg("Network unreachable, packet not sent!");
		free(out_if);
		return;
	}

	// find the interface by name
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(out_if, subsystem->ifaces[i].name))
			break;
	}
	pthread_rwlock_unlock(&subsystem->if_lock);
	
	if (i >= subsystem->num_ifaces){
		errorMsg("Given interfaces does not exist");
		free(out_if);
		return;
	}

	pthread_rwlock_rdlock(&subsystem->if_lock);
	if (subsystem->ifaces[i].enabled == 0){
		//errorMsg("Given interface is disabled");
		pthread_rwlock_unlock(&subsystem->if_lock);
		free(out_if);
		return;		
	}			
	uint8_t *myMAC = subsystem->ifaces[i].addr;
	pthread_rwlock_unlock(&subsystem->if_lock);

	// fill Ethernet Header
	for (i = 0; i < 6; i++) packet[i] = 0;
	for (i = 6, j = 0; i < 12; i++, j++) packet[i] = myMAC[j];	
	packet[12] = 8; packet[13] = 0;
	
	// get destination MAC
	uint8_t *dstMAC = arpLookupTree(subsystem->arpTree, ip);
	
	//for(i = 0; i < len; i++) printf("%d: %d\n", i, packet[i]);

	if(dstMAC){
		dbgMsg("Sending packet");
		for (i = 0; i < 6; i++) packet[i] = dstMAC[i];
		sr_integ_low_level_output(sr, packet, len, out_if);	
		free(dstMAC);	
	}
	else{ // send out ARP and queue the packet
		dbgMsg("Queueing packet");
		sendARPrequest(sr, out_if, ip);
		queuePacket(packet, len, out_if, ip);
	}	
	
	free(out_if);
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

void inorderPrintTree(arpTreeNode *node) {
    uint8_t ip_str[4];
    if(node == NULL) return;
    if(node->left) inorderPrintTree(node->left);
    int2byteIP(node->ip, ip_str);
    printf("ip: %u.%u.%u.%u mac:%x:%x:%x:%x:%x:%x\n", 
	    ip_str[0], ip_str[1], ip_str[2], ip_str[3],
	    node->mac[0], node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5]);
    if(node->right) inorderPrintTree(node->right);
}

void printARPCache() {
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    pthread_mutex_lock(&list_lock);
    struct arpCacheNode *node = subsystem->arpList;
    uint8_t ip_str[4];
    while(node != NULL) {
		int2byteIP(node->ip, ip_str);
		printf("ip: %u.%u.%u.%u mac:%2x:%2x:%2x:%2x:%2x:%2x static:%d\n", 
			ip_str[0], ip_str[1], ip_str[2], ip_str[3],
			node->mac[0], node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5],
			node->is_static);
		node = node->next;
	    }
    pthread_mutex_unlock(&list_lock);
}

void fill_rtable(rtableNode **head)
{
    int ip[4], gw[4], nm[4];
    char output_if[SR_NAMELEN+4];
    uint32_t ip_32 = 0, gw_32 = 0, nm_32 = 0;
    int i;
    FILE *rtable_file = fopen("rtable", "r");

	if (rtable_file == NULL) return;

	// initialize
	*head = NULL;
	
    while (!feof(rtable_file)) {
    	int mode = 0;
		if (fscanf(rtable_file, "%d.%d.%d.%d  %d.%d.%d.%d  %d.%d.%d.%d  %s", 
			    &ip[0], &ip[1], &ip[2], &ip[3],
			    &gw[0], &gw[1], &gw[2], &gw[3],
			    &nm[0], &nm[1], &nm[2], &nm[3],
			    output_if) != 13)
		    break;
		for(i = 0; i < 4; i++) {
		    ip_32 = ip_32 << 8;
		    ip_32 += (uint32_t)ip[i];
		    gw_32 = gw_32 << 8;
		    gw_32 += (uint32_t)gw[i];
		    nm_32 = nm_32 << 8;
		    nm_32 += (uint32_t)nm[i];
		}
		int if_len = strlen(output_if);
		if(if_len > 4){
			if( strcmp(&output_if[if_len-4], "...m") == 0 ){
				output_if[if_len-4] = 0;
				mode = 1;
			}
			else if( strcmp(&output_if[if_len-4], "...f") == 0 ){
				output_if[if_len-4] = 0;
				mode = 2;
			}
		}
		printf("Added to the routing table : %d.%d.%d.%d  %d.%d.%d.%d  %d.%d.%d.%d  %s\n", 
			    ip[0], ip[1], ip[2], ip[3],
			    gw[0], gw[1], gw[2], gw[3],
			    nm[0], nm[1], nm[2], nm[3],
			    output_if);
		printf("%x  %x  %x  %s\n", ip_32, gw_32, nm_32, output_if);
	    char *tmp_if = (char*)malloc(sizeof(char)*SR_NAMELEN);
	    strcpy(tmp_if, output_if);
	    if(mode == 1)	
			merge_rtable_node(head, ip_32, nm_32, &gw_32, &tmp_if, 1, 1);
	    else if(mode == 2)	
			force_insert_rtable_node(head, ip_32, nm_32, &gw_32, &tmp_if, 1, 1);
	    else
			insert_rtable_node(head, ip_32, nm_32, &gw_32, &tmp_if, 1, 1);
		free(tmp_if);
		ip_32 = gw_32 = nm_32 = 0;
    }

    fclose(rtable_file);

#ifdef _CPUMODE_
	pthread_mutex_lock(&rtable_lock);
		writeRoutingTable();
	pthread_mutex_unlock(&rtable_lock);
#endif // _CPUMODE_
}


// Thread monitors link status and enables/disables interface
void linkStatusThread(void *dummy){
	uint32_t mac_hi, mac_lo, stat;
	uint8_t mac[4][6];
	int i, j, k;

	struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	
	// read in all MACs to match interface names and status
	pthread_rwlock_rdlock(&subsystem->if_lock);

	pthread_mutex_lock(&ifRegLock);
	
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_0_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_0_LO_REG, &mac_lo);
	mac[0][0] = (mac_hi >> 8) & 0xFF;
	mac[0][1] = (mac_hi) & 0xFF;
	mac[0][2] = (mac_lo >> 24) & 0xFF;
	mac[0][3] = (mac_lo >> 16) & 0xFF;
	mac[0][4] = (mac_lo >> 8) & 0xFF;
	mac[0][5] = (mac_lo) & 0xFF;

	readReg(&netFPGA, ROUTER_OP_LUT_MAC_1_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_1_LO_REG, &mac_lo);
	mac[1][0] = (mac_hi >> 8) & 0xFF;
	mac[1][1] = (mac_hi) & 0xFF;
	mac[1][2] = (mac_lo >> 24) & 0xFF;
	mac[1][3] = (mac_lo >> 16) & 0xFF;
	mac[1][4] = (mac_lo >> 8) & 0xFF;
	mac[1][5] = (mac_lo) & 0xFF;

	readReg(&netFPGA, ROUTER_OP_LUT_MAC_2_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_2_LO_REG, &mac_lo);
	mac[2][0] = (mac_hi >> 8) & 0xFF;
	mac[2][1] = (mac_hi) & 0xFF;
	mac[2][2] = (mac_lo >> 24) & 0xFF;
	mac[2][3] = (mac_lo >> 16) & 0xFF;
	mac[2][4] = (mac_lo >> 8) & 0xFF;
	mac[2][5] = (mac_lo) & 0xFF;

	readReg(&netFPGA, ROUTER_OP_LUT_MAC_3_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_3_LO_REG, &mac_lo);
	mac[3][0] = (mac_hi >> 8) & 0xFF;
	mac[3][1] = (mac_hi) & 0xFF;
	mac[3][2] = (mac_lo >> 24) & 0xFF;
	mac[3][3] = (mac_lo >> 16) & 0xFF;
	mac[3][4] = (mac_lo >> 8) & 0xFF;
	mac[3][5] = (mac_lo) & 0xFF;

	pthread_mutex_unlock(&ifRegLock);

	int *link_status = (int*)malloc(sizeof(link_status)*subsystem->num_ifaces);
	for(i = 0; i < subsystem->num_ifaces; i++) link_status[i] = -1;

	pthread_rwlock_unlock(&subsystem->if_lock);	
	
	int fastreroute;
	while(1){
	
		pthread_mutex_lock(&subsystem->mode_lock);
			fastreroute = subsystem->mode & 0x2;
		pthread_mutex_unlock(&subsystem->mode_lock);

		readReg(&netFPGA, ROUTER_OP_LUT_LINK_STATUS_REG, &stat);
			
		pthread_rwlock_rdlock(&subsystem->if_lock);	
		for(i = 0; i < subsystem->num_ifaces; i++){
			for(j = 0; j < 4; j++){
				int match = 1;
				for(k = 0; k < 6; k++){
					if(mac[j][k] != subsystem->ifaces[i].addr[k]){
						match = 0;
						break;
					}
				}
				if(match){
					int new_status = (stat >> (2*j)) & 0x01;
					if(new_status != link_status[i]){
						link_status[i] = new_status;
			
						char tmp_name[SR_NAMELEN];
						strcpy(tmp_name, subsystem->ifaces[i].name);
						pthread_rwlock_unlock(&subsystem->if_lock);
						if(fastreroute){	
							router_interface_set_enabled(sr, tmp_name, link_status[i]);
						}
						else{
							router_interface_set_enabled_only(sr, tmp_name, link_status[i]);
						}
						pthread_rwlock_rdlock(&subsystem->if_lock);
							
					}
					break;
				}
			}
		}
		pthread_rwlock_unlock(&subsystem->if_lock);	
		
		
		usleep(LINK_STATUS_REFRESH);
	}
	
	free(link_status);
}

/**
 * ---------------------------------------------------------------------------
 * -------------------- CLI Functions ----------------------------------------
 * ---------------------------------------------------------------------------
 */


/**
 * Enables or disables an interface on the router.
 * @return 0 if name was enabled
 *         -1 if it does not not exist
 *         1 if already set to enabled
 */
int router_interface_set_enabled( struct sr_instance* sr, const char* name, int enabled ) {
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int i;
	pthread_rwlock_wrlock(&subsystem->if_lock);
    for(i = 0; i < subsystem->num_ifaces; i++) {
		if(!strcmp(subsystem->ifaces[i].name, name)) {
		    if(subsystem->ifaces[i].enabled == enabled && subsystem->ifaces[i].hard_enabled){
				pthread_rwlock_unlock(&subsystem->if_lock);
				return 1;
			}
		    else {
				subsystem->ifaces[i].enabled = enabled && subsystem->ifaces[i].hard_enabled;
				pthread_rwlock_unlock(&subsystem->if_lock);
				updateNeighbors();
				sendLSU();
				update_rtable();
				return 0;
		    }
		}
    }
	pthread_rwlock_unlock(&subsystem->if_lock);
    return -1;
}

/**
 * Enables an interface on the router.
 * @return 0 if name was enabled
 *         -1 if it does not not exist
 *         1 if already set to enabled
 */
int router_interface_set_enabled_only( struct sr_instance* sr, const char* name, int enabled ) {
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int i;
    if(enabled == 0) return 0;
    
	pthread_rwlock_wrlock(&subsystem->if_lock);
    for(i = 0; i < subsystem->num_ifaces; i++) {
		if(!strcmp(subsystem->ifaces[i].name, name)) {
		    if(subsystem->ifaces[i].enabled == enabled && subsystem->ifaces[i].hard_enabled){
				pthread_rwlock_unlock(&subsystem->if_lock);
				return 1;
			}
		    else {
				subsystem->ifaces[i].enabled = enabled && subsystem->ifaces[i].hard_enabled;
				pthread_rwlock_unlock(&subsystem->if_lock);
				return 0;
		    }
		}
    }
	pthread_rwlock_unlock(&subsystem->if_lock);
    return -1;
}

/**
 * Enables or disables an interface on the router (for use by CLI -- overrides enabled).
 * @return 0 if name was enabled
 *         -1 if it does not not exist
 *         1 if already set to enabled
 */
int router_interface_set_hard_enabled( struct sr_instance* sr, const char* name, int enabled ) {
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int i;
	pthread_rwlock_wrlock(&subsystem->if_lock);
    for(i = 0; i < subsystem->num_ifaces; i++) {
		if(!strcmp(subsystem->ifaces[i].name, name)) {
		    if(subsystem->ifaces[i].hard_enabled == enabled){
				pthread_rwlock_unlock(&subsystem->if_lock);
				return 1;
			}
		    else {
				subsystem->ifaces[i].hard_enabled = enabled;
				subsystem->ifaces[i].enabled = subsystem->ifaces[i].enabled && enabled;
				pthread_rwlock_unlock(&subsystem->if_lock);
				updateNeighbors();
				sendLSU();
				update_rtable();
				return 0;
		    }
		}
    }
	pthread_rwlock_unlock(&subsystem->if_lock);
    return -1;
}

/**
 * Returns a pointer to the interface which is assigned the specified IP.
 *
 * @return interface, or NULL if the IP does not belong to any interface
 *         (you'll want to change void* to whatever type you end up using)
 */
struct sr_vns_if* router_lookup_interface_via_ip( struct sr_instance* sr,
                                      uint32_t ip ) {
    struct sr_vns_if *interface = NULL;
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int i;
    pthread_rwlock_rdlock(&subsystem->if_lock);
    for(i = 0; i < subsystem->num_ifaces; i++) {
		if(subsystem->ifaces[i].ip == ip) {
		    interface = &(subsystem->ifaces[i]);
		    break;
		}
    }
    pthread_rwlock_unlock(&subsystem->if_lock);
    return interface;
}

/**
 * Returns a pointer to the interface described by the specified name.
 *
 * @return interface, or NULL if the name does not match any interface
 *         (you'll want to change void* to whatever type you end up using)
 */
struct sr_vns_if* router_lookup_interface_via_name( struct sr_instance* sr,
                                        const char* name ) {
    struct sr_vns_if *interface = NULL;
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int i;
    pthread_rwlock_rdlock(&subsystem->if_lock);
    for(i = 0; i < subsystem->num_ifaces; i++) {
		if(!strcmp(subsystem->ifaces[i].name, name)) {
		    interface = &(subsystem->ifaces[i]);
		    break;
		}
    }
    pthread_rwlock_unlock(&subsystem->if_lock);
    return interface;
}

/**
 * Returns 1 if the specified interface is up and 0 otherwise.
 */
int router_is_interface_enabled( struct sr_instance* sr, void* intf ) {
    struct sr_vns_if *interface = (struct sr_vns_if*)intf;
    return interface->enabled;
}

/**
 * Returns whether OSPF is enabled (0 if disabled, otherwise it is enabled).
 */
int router_is_ospf_enabled( struct sr_instance* sr ) {
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    return subsystem->ospf_enabled;
}

/**
 * Sets whether OSPF is enabled.
 */
void router_set_ospf_enabled( struct sr_instance* sr, int enabled ) {
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	subsystem->ospf_enabled = enabled;
}

// enable/disable multipath
// returns router mode, or -1 on error
int setMultipath(int multipath){
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	pthread_mutex_lock(&subsystem->mode_lock);
	subsystem->mode &= ~0x1;
	if(multipath){
		subsystem->mode |= 0x1;
		#ifdef _CPUMODE_
	    writeReg(&netFPGA, ROUTER_OP_LUT_MULTIPATH_ENABLE_REG, 0x1);
		#endif //_CPUMODE_
	}
	else{
		#ifdef _CPUMODE_
	    writeReg(&netFPGA, ROUTER_OP_LUT_MULTIPATH_ENABLE_REG, 0x0);
		#endif //_CPUMODE_
	}
	pthread_mutex_unlock(&subsystem->mode_lock);
	return getMode();
}
// enable/disable fast reroute
// returns router mode, or -1 on error
int setFastReroute(int fast){
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	pthread_mutex_lock(&subsystem->mode_lock);
	subsystem->mode &= ~0x2;
	if(fast){
		subsystem->mode |= 0x2;
		#ifdef _CPUMODE_
	    writeReg(&netFPGA, ROUTER_OP_LUT_FAST_REROUTE_ENABLE_REG, 0x1);
		#endif //_CPUMODE_
	}
	else{
		#ifdef _CPUMODE_
	    writeReg(&netFPGA, ROUTER_OP_LUT_FAST_REROUTE_ENABLE_REG, 0x0);
		#endif //_CPUMODE_	
	}
	pthread_mutex_unlock(&subsystem->mode_lock);
	return getMode();
}
// returns router mode
int getMode(){
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	int retVal;
	uint32_t multipath_val, fast_val;

	pthread_mutex_lock(&subsystem->mode_lock);

	#ifdef _CPUMODE_
	readReg(&netFPGA, ROUTER_OP_LUT_MULTIPATH_ENABLE_REG, &multipath_val);
    readReg(&netFPGA, ROUTER_OP_LUT_FAST_REROUTE_ENABLE_REG, &fast_val);
	subsystem->mode = (fast_val << 1) + multipath_val;
	#endif //_CPUMODE_

	retVal = subsystem->mode;
	pthread_mutex_unlock(&subsystem->mode_lock);

	return retVal;
}

uint32_t findMinNetmask_(uint32_t s1, uint32_t s2){
	uint32_t retVal = 0x0;
	uint32_t msk;
	uint32_t s = s1 ^ s2;
	for(msk = 0x80000000; msk; msk >>= 1){
		retVal |= msk;
		if( s & msk ) break;
	}
	return retVal;
}

int compareRoutes_(rtableNode* r1, rtableNode* r2){
	int i, j;
	
	if(r1->out_cnt != r2->out_cnt) return 0;
	if(r1->entry_index != r2->entry_index) return 0;
	for(i = 0; i < r1->out_cnt; i++){
		int tmp_found = 0;
		for(j = 0; j < r2->out_cnt; j++){
			if(r1->gateway[i] == r2->gateway[j]){
				if(!strcmp(r1->output_if[i], r2->output_if[j])){
					tmp_found = 1;
					break;
				}
				else{
					return 0;
				}
			}
		}
		if(tmp_found == 0) return 0;
	}
	return 1;	
}

void aggregateRoutes(rtableNode** rtable){
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	pthread_mutex_lock(&subsystem->mode_lock);
		int fast_reroute = subsystem->mode & 0x2;
	pthread_mutex_unlock(&subsystem->mode_lock);

	rtableNode* first = *rtable;
	while(first){
		// find max mask
		uint32_t max_msk = 0x0;
		
		// aggregate nodes down
		if(first->entry_index == 0 && fast_reroute){
			rtableNode* second = *rtable;
			while(second){
				if(second->entry_index == first->entry_index){
					if(!compareRoutes_(first, second)){
						uint32_t tmp_msk = findMinNetmask_(first->ip & first->netmask, second->ip & second->netmask);
						if(tmp_msk > max_msk) max_msk = tmp_msk;
					}
				}
				second = second->next;
			}
			second = first->next;
			while(second){
				rtableNode* second_next = second->next;
				if(second->entry_index == first->entry_index){
					if( (first->netmask <= second->netmask) && ( (first->ip & max_msk) == (second->ip & max_msk) ) ){
						uint32_t tmp_msk = findMinNetmask_(first->ip & first->netmask, second->ip & second->netmask);
						if(tmp_msk != 0xFFFFFFFF) tmp_msk <<= 1;
						first->netmask &= tmp_msk & second->netmask;
					
						if(second->next) second->next->prev = second->prev;
						second->prev->next = second->next;
						free(second);
					}
				}
				second = second_next;
			}
		} 
		// aggregate nodes up
		else if(first->entry_index == 1 || !fast_reroute){ 
			rtableNode* second = *rtable;
			while(second){
				if(second->entry_index == first->entry_index){
					if(!compareRoutes_(first, second)){
						uint32_t tmp_msk = findMinNetmask_(first->ip & first->netmask, second->ip & second->netmask);
						if(tmp_msk > max_msk) max_msk = tmp_msk;
					}
				}
				second = second->next;
			}
			second = first->prev;
			while(second){
				rtableNode* second_prev = second->prev;
				if(second->entry_index == first->entry_index){
					if( (first->ip & max_msk) == (second->ip & max_msk) ){
						uint32_t tmp_msk = findMinNetmask_(first->ip & first->netmask, second->ip & second->netmask);
						if(tmp_msk != 0xFFFFFFFF) tmp_msk <<= 1;
						first->netmask &= tmp_msk & second->netmask;
					
						second->next->prev = second->prev;
						if(second->prev){ 	
							second->prev->next = second->next;
						}
						else{
							*rtable = second->next;
						}
						free(second);
					}
				}
				second = second_prev;
			}		
		}
		first = first->next;
	}	
		
}

#ifdef _CPUMODE_

// writes IP filter to hardware
void writeIPfilter(){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	pthread_mutex_lock(&filtRegLock);
	pthread_rwlock_rdlock(&subsystem->if_lock);
	
	for(i = 0; i < subsystem->num_ifaces; i++){
		writeReg(&netFPGA, ROUTER_OP_LUT_DST_IP_FILTER_TABLE_ENTRY_IP_REG, subsystem->ifaces[i].ip);
		writeReg(&netFPGA, ROUTER_OP_LUT_DST_IP_FILTER_TABLE_WR_ADDR_REG, i);
	}
	// write 224.0.0.5
	writeReg(&netFPGA, ROUTER_OP_LUT_DST_IP_FILTER_TABLE_ENTRY_IP_REG, ALLSPFRouters);
	writeReg(&netFPGA, ROUTER_OP_LUT_DST_IP_FILTER_TABLE_WR_ADDR_REG, i);
	i++;
	for(; i < ROUTER_OP_LUT_DST_IP_FILTER_TABLE_DEPTH ; i++){
		writeReg(&netFPGA, ROUTER_OP_LUT_DST_IP_FILTER_TABLE_ENTRY_IP_REG, 0);
		writeReg(&netFPGA, ROUTER_OP_LUT_DST_IP_FILTER_TABLE_WR_ADDR_REG, i);
	}

	pthread_rwlock_unlock(&subsystem->if_lock);
	pthread_mutex_unlock(&filtRegLock);
}


// writes ARP cache to hardware
// caller mush hold read lock on tree_lock
void writeARPCache(arpTreeNode *node, int *index){
    if(node == NULL) return;
    if(node->left) writeARPCache(node->left, index);

	unsigned int mac_hi;
	unsigned int mac_lo;
	uint8_t *mac_addr = node->mac;
	mac_hi = 0;
	mac_lo = 0;
	mac_hi |= ((unsigned int)mac_addr[0]) << 8;
	mac_hi |= ((unsigned int)mac_addr[1]);
	mac_lo |= ((unsigned int)mac_addr[2]) << 24;
	mac_lo |= ((unsigned int)mac_addr[3]) << 16;
	mac_lo |= ((unsigned int)mac_addr[4]) << 8;
	mac_lo |= ((unsigned int)mac_addr[5]);

	if(*index < ROUTER_OP_LUT_ARP_TABLE_DEPTH){
		pthread_mutex_lock(&arpRegLock);
		writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_ENTRY_NEXT_HOP_IP_REG, node->ip );
		writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_ENTRY_MAC_HI_REG, mac_hi );
		writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_ENTRY_MAC_LO_REG, mac_lo );
		writeReg( &netFPGA, ROUTER_OP_LUT_ARP_TABLE_WR_ADDR_REG, (*index)++ );
		pthread_mutex_unlock(&arpRegLock);
	}
    if(node->right) writeARPCache(node->right, index);
}

// writes routing table to hardware
// caller must hold rtable_lock
void writeRoutingTable(){
	int i;
	int index = 0;
	uint32_t mac_hi, mac_lo;
	uint8_t mac[4][6];
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	pthread_mutex_lock(&subsystem->mode_lock);
		int fast_reroute = subsystem->mode & 0x2;
	pthread_mutex_unlock(&subsystem->mode_lock);

	rtableNode *cpy_rtable = copy_rtable(subsystem->rtable);
	rtableNode *agg_rtable = NULL;
	
	aggregateRoutes(&cpy_rtable);
	
	if(fast_reroute){
		agg_rtable = cpy_rtable;
	}
	else{
		rebuild_rtable_lockless(&agg_rtable, cpy_rtable);
	}

	// read in all MACs to match interface names
	pthread_rwlock_rdlock(&subsystem->if_lock);

	pthread_mutex_lock(&ifRegLock);
	
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_0_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_0_LO_REG, &mac_lo);
	mac[0][0] = (mac_hi >> 8) & 0xFF;
	mac[0][1] = (mac_hi) & 0xFF;
	mac[0][2] = (mac_lo >> 24) & 0xFF;
	mac[0][3] = (mac_lo >> 16) & 0xFF;
	mac[0][4] = (mac_lo >> 8) & 0xFF;
	mac[0][5] = (mac_lo) & 0xFF;

	readReg(&netFPGA, ROUTER_OP_LUT_MAC_1_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_1_LO_REG, &mac_lo);
	mac[1][0] = (mac_hi >> 8) & 0xFF;
	mac[1][1] = (mac_hi) & 0xFF;
	mac[1][2] = (mac_lo >> 24) & 0xFF;
	mac[1][3] = (mac_lo >> 16) & 0xFF;
	mac[1][4] = (mac_lo >> 8) & 0xFF;
	mac[1][5] = (mac_lo) & 0xFF;

	readReg(&netFPGA, ROUTER_OP_LUT_MAC_2_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_2_LO_REG, &mac_lo);
	mac[2][0] = (mac_hi >> 8) & 0xFF;
	mac[2][1] = (mac_hi) & 0xFF;
	mac[2][2] = (mac_lo >> 24) & 0xFF;
	mac[2][3] = (mac_lo >> 16) & 0xFF;
	mac[2][4] = (mac_lo >> 8) & 0xFF;
	mac[2][5] = (mac_lo) & 0xFF;

	readReg(&netFPGA, ROUTER_OP_LUT_MAC_3_HI_REG, &mac_hi);
	readReg(&netFPGA, ROUTER_OP_LUT_MAC_3_LO_REG, &mac_lo);
	mac[3][0] = (mac_hi >> 8) & 0xFF;
	mac[3][1] = (mac_hi) & 0xFF;
	mac[3][2] = (mac_lo >> 24) & 0xFF;
	mac[3][3] = (mac_lo >> 16) & 0xFF;
	mac[3][4] = (mac_lo >> 8) & 0xFF;
	mac[3][5] = (mac_lo) & 0xFF;

	pthread_mutex_unlock(&ifRegLock);

	gwList_flush(&subsystem->gwList);
	
	pthread_mutex_lock(&routeRegLock);

	rtableNode *rtable;
	if(subsystem->agg_enabled)
		rtable = agg_rtable;
	else
		rtable = subsystem->rtable;

	while(rtable){
		uint32_t ifs = 0;
		uint32_t gws = 0;
		int pos = -1;
		if(index < ROUTER_OP_LUT_ROUTE_TABLE_DEPTH){
			writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_IP_REG, rtable->ip );
			writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_MASK_REG, rtable->netmask );
			
			for(i = 0; i < rtable->out_cnt; i++){
				char *name;
				name = getIfNameFromMAC(&mac[3][0]);
				if(name && ( strcmp(name, rtable->output_if[i]) == 0 )){
					pos = gwList_insert(&subsystem->gwList, rtable->gateway[i]);	
					if(pos != -1){
						ifs |= 0x40;
						gws = (gws & ~0xFF000000) | ( ((uint32_t)(pos & 0xFF)) << 24 );					
					}
				}
				name = getIfNameFromMAC(&mac[2][0]);
				if(name && ( strcmp(name, rtable->output_if[i]) == 0 )){
					pos = gwList_insert(&subsystem->gwList, rtable->gateway[i]);	
					if(pos != -1){
						ifs |= 0x10;
						gws = (gws & ~0x00FF0000) | ( ((uint32_t)(pos & 0xFF)) << 16 );					
					}
				}
				name = getIfNameFromMAC(&mac[1][0]);
				if(name && ( strcmp(name, rtable->output_if[i]) == 0 )){
					pos = gwList_insert(&subsystem->gwList, rtable->gateway[i]);	
					if(pos != -1){
						ifs |= 0x04;
						gws = (gws & ~0x0000FF00) | ( ((uint32_t)(pos & 0xFF)) << 8 );					
					}
				}
				name = getIfNameFromMAC(&mac[0][0]);
				if(name && ( strcmp(name, rtable->output_if[i]) == 0 )){
					pos = gwList_insert(&subsystem->gwList, rtable->gateway[i]);	
					if(pos != -1){
						ifs |= 0x01;
						gws = (gws & ~0x000000FF) | ( ((uint32_t)(pos & 0xFF)) << 0 );					
					}
				}
			}			
			writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_NEXT_HOP_IP_REG, gws );
			writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_OUTPUT_PORT_REG, ifs );
			writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_WR_ADDR_REG, index++ );		
		}
		else{
			break;
		}		
		rtable = rtable->next;
	}
	// fill the rest of the table with 0s
	for(i = index; i < ROUTER_OP_LUT_ROUTE_TABLE_DEPTH; i++){
		writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_IP_REG, 0 );
		writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_MASK_REG, 0 );
		writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_NEXT_HOP_IP_REG, 0 );
		writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_ENTRY_OUTPUT_PORT_REG, 0 );
		writeReg( &netFPGA, ROUTER_OP_LUT_ROUTE_TABLE_WR_ADDR_REG, i );		
	}	
	pthread_mutex_unlock(&routeRegLock);

	pthread_mutex_lock(&gw_lock);
	pthread_mutex_lock(&gwRegLock);
	index = 0;
	struct gwListNode *gwNode = subsystem->gwList;
	while(gwNode){
		if(index < ROUTER_OP_LUT_GATEWAY_TABLE_DEPTH){
			writeReg( &netFPGA, ROUTER_OP_LUT_GATEWAY_TABLE_ENTRY_IP_REG, gwNode->gw );
			writeReg( &netFPGA, ROUTER_OP_LUT_GATEWAY_TABLE_WR_ADDR_REG, index++ );				
		}
		else{
			break;
		}
		gwNode = gwNode->next;
	}
	// fill the rest of the table with 255.255.255.255 (because 0 is actually a valid gw)
	for(i = index; i < ROUTER_OP_LUT_GATEWAY_TABLE_DEPTH; i++){
			writeReg( &netFPGA, ROUTER_OP_LUT_GATEWAY_TABLE_ENTRY_IP_REG, 0xFFFFFFFF );
			writeReg( &netFPGA, ROUTER_OP_LUT_GATEWAY_TABLE_WR_ADDR_REG, i );				
	}	
	pthread_mutex_unlock(&gwRegLock);
	pthread_mutex_unlock(&gw_lock);
	
	pthread_rwlock_unlock(&subsystem->if_lock);
	
	kill_rtable(&agg_rtable);

}

#endif // _CPUMODE
