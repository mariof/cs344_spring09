#include "router.h"

void pwospfSendHelloThread(void* arg){
	struct pwospf_if* iface = (struct pwospf_if*)arg;
	while(1){
		sendHello(iface->ip);
		pthread_mutex_lock(&iface->neighbor_lock);
			struct pwospf_neighbor* nbor = iface->neighbor_list;
			struct pwospf_neighbor* prev_nbor = NULL;
			while(nbor){
				if( (time(NULL) - nbor->lastHelloTime) > (3 * iface->helloint) ){
					if(prev_nbor){
						prev_nbor->next = nbor->next;
						free(nbor);
						nbor = prev_nbor->next;
						/* 
							TODO: send LSU updates (note: still holding neighbor lock here)						
						*/
					}
				}
				else{
					prev_nbor = nbor;
					nbor = nbor->next;
				}
			}
		pthread_mutex_unlock(&iface->neighbor_lock);		
		sleep(iface->helloint);
	}
}

void pwospfSendLSUThread(void* dummy){
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	while(1){
		sleep(subsystem->pwospf.lsuint);
	}
}

void processPWOSPF(const char* interface, uint8_t* packet, unsigned len){
	int i;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	struct pwospf_if* iface = NULL;
	
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(interface, subsystem->ifaces[i].name)){
			iface = findPWOSPFif(&subsystem->pwospf, subsystem->ifaces[i].ip);
		 	break;
		 }
	}	
	
	if(!iface){
		errorMsg("processPWOSPF: interface not found");
		return;
	}
	
    if (len < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH){
    	errorMsg("PWOSPF Packet too short");
    	return;
    }

	if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 0] != 2) {
	    errorMsg("OSPF Version error! Dropping the packet.");
	    return;
	}


	// CHECKSUM
	uint32_t ospfLen = len - (ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH);
	uint8_t* ospfPacket = (uint8_t*)malloc(ospfLen*sizeof(uint8_t));
	memcpy(ospfPacket, &packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH], ospfLen);
    for(i = 16; i < 24; i++) ospfPacket[i] = 0; // set Authentication fields to 0 to check checksum
    
   	// check checksum
	uint16_t word16, sum16;
	uint32_t sum = 0;
	    
	// make 16 bit words out of every two adjacent 8 bit words in the packet
	// and add them up
	for (i = 0; i < ospfLen; i+=2) {
	    word16 = ospfPacket[i] & 0xFF;
	    word16 = (word16 << 8) + (ospfPacket[i+1] & 0xFF);
	    sum += (uint32_t)word16;	
	}

	// take only 16 bits out of the 32 bit sum and add up the carries
	while (sum >> 16)
	    sum = (sum & 0xFFFF) + (sum >> 16);
	sum16 = ~((uint16_t)(sum & 0xFFFF));

	free(ospfPacket);
	if(sum16 != 0) {
	    /* checksum error
	     * drop packet
	     */
	    errorMsg("OSPF Checksum error! Dropping the packet.");
	    return;
	}

	// check AREA ID
	uint32_t areaID = ntohl(*(uint32_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 8]));
	if(areaID != subsystem->pwospf.areaID){
		errorMsg("OSPF areaID missmatch! Dropping the packet.");
		return;
	}
	
	// check AUTHENTICATION TYPE
	if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 14] != 0) {
	    errorMsg("OSPF authentication type error! Dropping the packet.");
	    return;
	}
	
	
	// check NETMASK
	uint32_t netmask = ntohl(*(uint32_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH]));
	if(netmask != iface->netmask) {
	    errorMsg("OSPF netmask missmatch! Dropping the packet.");
	    return;
	}
	
	// check HELLOINT
	uint16_t helloint = ntohs(*(uint16_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 4]));
	if(helloint != iface->helloint) {
	    errorMsg("OSPF helloint missmatch! Dropping the packet.");
	    return;
	}


	uint32_t srcIP = packet[ETHERNET_HEADER_LENGTH + 12] * 256 * 256 * 256 +
					 packet[ETHERNET_HEADER_LENGTH + 13] * 256 * 256 +
					 packet[ETHERNET_HEADER_LENGTH + 14] * 256 +
					 packet[ETHERNET_HEADER_LENGTH + 15] * 1;    				
    srcIP = ntohl(srcIP);

	uint32_t routerID = ntohl(*(uint32_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 4]));
    
    // process packet
	if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 1] == 1){ // Hello packet
		pthread_mutex_lock(&iface->neighbor_lock);
			struct pwospf_neighbor* nbor = findOSPFNeighbor(iface, srcIP);
			if(nbor){
				nbor->lastHelloTime = time(NULL);
			}
			else{
				nbor = (struct pwospf_neighbor*)malloc(sizeof(struct pwospf_neighbor));
				nbor->id = routerID;
				nbor->ip = srcIP;
				nbor->lastHelloTime = time(NULL);
				nbor->next = iface->neighbor_list;
				iface->neighbor_list = nbor;
			}
		pthread_mutex_unlock(&iface->neighbor_lock);
	}
	else if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 1] == 4){ // LSU packet
	
	}
}

void sendHello(uint32_t ifIP){
	int i, j, k;
	const unsigned len = 66; // 14 ethernet + 20 ip + 24 ospf + 8 hello
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	uint8_t *p = (uint8_t*)malloc(len*sizeof(uint8_t));

	// fill Ethernet Header
	for (i = 0; i < 6; i++) p[i] = 255;
	for (i = 6, j = 0; i < 12; i++, j++) p[i] = 0;	
	p[12] = 8; p[13] = 0;

	// IP header
	i = ETHERNET_HEADER_LENGTH;
	p[i++] = 69; // version and length 
	p[i++] = 0; // TOS
	p[i++] = 0; // total length
	p[i++] = len - ETHERNET_HEADER_LENGTH; // total length
	p[i++] = (uint8_t)(rand() % 256); p[i++] = (uint8_t)(rand() % 256); // identification
	p[i++] = 0; p[i++] = 0; // fragmentation
	p[i++] = 64; // TTL
	p[i++] = 89; // protocol (OSPF)
	p[i++] = 0; p[i++] = 0; // checksum (calculated later)
	i += 4; // place for src IP
	int2byteIP(ntohl(ALLSPFRouters), &p[i]); i+=4; // destination IP
	
	// OSPF header
	p[i++] = 2; // version
	p[i++] = 1; // type (Hello)
	p[i++] = 0; // ospf length (header + data) -> MSB
	p[i++] = 32; // ospf length (header + data) -> LSB
	*((uint32_t*)&p[i]) = htonl(subsystem->pwospf.routerID); i+=4; // router ID
	*((uint32_t*)&p[i]) = htonl(subsystem->pwospf.areaID); i+=4; // area ID
	p[i++] = 0; p[i++] = 0; // checksum (calculated later)
	p[i++] = 0; p[i++] = 0; // Autype
	p[i++] = 0; p[i++] = 0; p[i++] = 0; p[i++] = 0; // Authentication #1
	p[i++] = 0; p[i++] = 0; p[i++] = 0; p[i++] = 0; // Authentication #2

	for(i = 0; i < subsystem->num_ifaces; i++){
		if(subsystem->ifaces[i].enabled && subsystem->ifaces[i].ip == ifIP){
	
			// update Ethernet Header
			uint8_t *myMAC = subsystem->ifaces[i].addr;
			for (k = 6, j = 0; k < 12; k++, j++) p[k] = myMAC[j];	

			// src ip
			int2byteIP(subsystem->ifaces[i].ip, &p[ETHERNET_HEADER_LENGTH + 12]); // source IP

			// IP checksum
			p[ETHERNET_HEADER_LENGTH + 10] = 0; p[ETHERNET_HEADER_LENGTH + 11] = 0; // checksum (calculated later)
			uint16_t ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
			p[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
			p[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum
		
			// Hello header
			struct pwospf_if* pwif = findPWOSPFif(&subsystem->pwospf, subsystem->ifaces[i].ip);
			if(pwif == NULL){
				errorMsg("Hello header: unknown interface");	
				break;
			}
			*((uint32_t*)&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 0]) = 
				htonl(pwif->netmask); // netmask
			*((uint16_t*)&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 4]) = 
				htons(pwif->helloint); // hello interval
			p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 6] = 0; // padding
			p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 7] = 0; // padding
			

			// OSPF checksum (make sure Authentication fileds are set to 0 here)
			p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 12] = 0; // checksum
			p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 13] = 0; // checksum
			uint16_t ospfChksum = 
				checksum(	(uint16_t*)(&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), 
							len - (ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH) );
			p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 12] = (htons(ospfChksum) >> 8) & 0xff; // OSPF checksum 
			p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 13] = (htons(ospfChksum) & 0xff); // OSPF checksum
			
		
			// send packet
			sr_integ_low_level_output(sr, p, len, subsystem->ifaces[i].name);	
			
			break;
		}
	}
	free(p);
}

// initilize the datastructures for pwospf
void initPWOSPF(struct sr_instance* sr){
	int i;
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	subsystem->pwospf.routerID = rand(); // this is [0, 32767] but still good enough
	subsystem->pwospf.areaID = 1; // this should be the same for all routers
	subsystem->pwospf.lsuint = LSUINT;
	subsystem->pwospf.if_list = NULL;

	for(i = 0; i < subsystem->num_ifaces; i++){
		struct pwospf_if* node = (struct pwospf_if*)malloc(sizeof(struct pwospf_if));
		node->ip = subsystem->ifaces[i].ip;
		node->netmask = subsystem->ifaces[i].mask;
		node->helloint = HELLOINT;
		pthread_mutex_init(&node->neighbor_lock, NULL);
		node->neighbor_list = NULL;
		node->next = subsystem->pwospf.if_list;
		
		subsystem->pwospf.if_list = node;		
	}	
}

struct pwospf_if* findPWOSPFif(struct pwospf_router* router, uint32_t ip){
	struct pwospf_if* node = router->if_list;
	while(node){
		if(node->ip == ip) return node;
		node = node->next;
	}
	return NULL;
}

// caller must hold lock on interface->neighbor_lock
struct pwospf_neighbor* findOSPFNeighbor(struct pwospf_if* interface, uint32_t ip){
	struct pwospf_neighbor* node = interface->neighbor_list;
	while(node){
		if(node->ip == ip) return node;
		node = node->next;
	}
	return NULL;
}
