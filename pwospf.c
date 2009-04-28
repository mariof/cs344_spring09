#include "router.h"

// Thread that sends Hello packets, one thread per interface
// Also checks for Hello timeouts (note: it might be better to do this with alarm toghether with LSUs)
void pwospfSendHelloThread(void* arg){
	struct pwospf_if* iface = (struct pwospf_if*)arg;
	while(1){
		int updateLSU = 0;
		sendHello(iface->ip);
		pthread_mutex_lock(&iface->neighbor_lock);
			struct pwospf_neighbor* nbor = iface->neighbor_list;
			struct pwospf_neighbor* prev_nbor = NULL;
			while(nbor){
				if( (time(NULL) - nbor->lastHelloTime) > (NEIGHBOR_TIMEOUT * iface->helloint) ){
					if(prev_nbor){
						prev_nbor->next = nbor->next;
						free(nbor);
						nbor = prev_nbor->next;
						dbgMsg("PWOSPF: Hello packet timeout");
						updateLSU = 1;
					}
				}
				else{
					prev_nbor = nbor;
					nbor = nbor->next;
				}
			}
		pthread_mutex_unlock(&iface->neighbor_lock);
	
		if(updateLSU){ 
			sendLSU();
		}		
	
		sleep(iface->helloint);
	}
}

// Thread that sends LSU packets
void pwospfSendLSUThread(void* dummy){
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	while(1){
		sendLSU();
		sleep(subsystem->pwospf.lsuint);
	}
}

// Processing all received PWOSPF packets
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
		if(isMyIP(srcIP)){
			errorMsg("LSU packet generated by the receiving router. Dropping the packet.");
			return;
		}

		// TODO: check sequence number in the topology database (drop if invalid)
		
		// TODO: check packet hash(?) in the topology database (ignore if the same)
		
		// TODO: update the topology database
		
		forwardLSUpacket(interface, packet, len);
		
	}
}

// forwards LSU packets to all interfaces except the incoming one, also decrements and checks TTL
void forwardLSUpacket(const char* incoming_if, uint8_t* packet, unsigned len){
	int i, j, k;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	// flood packet (ethernet flood)
	for (j = 0; j < 6; j++) packet[j] = 255;

	// update LSU TTL
	uint16_t lsu_ttl = ntohs(*(uint16_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 2]));		
	lsu_ttl--;
	*(uint16_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 2]) = htons(lsu_ttl);
	
	if(lsu_ttl <= 0){
		dbgMsg("LSU TTL expired!");
		return;
	}		

	// save OSPF auth fileds
	uint8_t tmp_auth[8];
	memcpy(tmp_auth, &packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 16], 8);
	memset(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 16], 0, 8);

	// OSPF checksum (make sure Authentication fileds are set to 0 here)
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 12] = 0; // checksum
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 13] = 0; // checksum
	uint16_t ospfChksum = 
		checksum(	(uint16_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), 
					len - (ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH) );
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 12] = (htons(ospfChksum) >> 8) & 0xff; // OSPF checksum 
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 13] = (htons(ospfChksum) & 0xff); // OSPF checksum
	
	// restore OSPF auth fields
	memcpy(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 16], tmp_auth, 8);


	// find the incoming interface by name and skip it
	for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(incoming_if, subsystem->ifaces[i].name) || !(subsystem->ifaces[i].enabled) ){
			continue;
		}

		// update Ethernet Header
		uint8_t *myMAC = subsystem->ifaces[i].addr;
		for (k = 6, j = 0; k < 12; k++, j++) packet[k] = myMAC[j];	
			
		// send packet
		sr_integ_low_level_output(sr, packet, len, subsystem->ifaces[i].name);			
	}
}

// generate and send a LSU packet from all enabled interfaces 
void sendLSU(){
	static uint16_t sequence = 0;
	int i, j, k;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	int len = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + OSPF_HEADER_LENGTH + 8;
	uint32_t advCnt = 0;
	uint8_t *packet;

	struct pwospf_if* iface = subsystem->pwospf.if_list;

	// count number of advertisements
	while(iface){
		if(isEnabled(iface->ip)){
			pthread_mutex_lock(&iface->neighbor_lock);
			struct pwospf_neighbor* nbor = iface->neighbor_list;
			while(nbor){
				advCnt++;
				nbor = nbor->next;
			}
			pthread_mutex_unlock(&iface->neighbor_lock);		
		}			
		iface = iface->next;
	}


	// calculate packet length
	len += advCnt * 12;
	packet = (uint8_t*)malloc(len*sizeof(uint8_t));


	// fill Ethernet Header
	for (i = 0; i < 6; i++) packet[i] = 255;
	for (i = 6, j = 0; i < 12; i++, j++) packet[i] = 0;	
	packet[12] = 8; packet[13] = 0;

	// IP header
	i = ETHERNET_HEADER_LENGTH;
	packet[i++] = 69; // version and length 
	packet[i++] = 0; // TOS
	*((uint16_t*)&packet[i]) = htons(len - ETHERNET_HEADER_LENGTH); i+=2; // total length
	packet[i++] = (uint8_t)(rand() % 256); packet[i++] = (uint8_t)(rand() % 256); // identification
	packet[i++] = 0; packet[i++] = 0; // fragmentation
	packet[i++] = 64; // TTL
	packet[i++] = 89; // protocol (OSPF)
	packet[i++] = 0; packet[i++] = 0; // checksum (calculated later)
	i += 4; // place for src IP
	int2byteIP(ntohl(ALLSPFRouters), &packet[i]); i+=4; // destination IP
	
	// OSPF header
	packet[i++] = 2; // version
	packet[i++] = 4; // type (LSU)
	*((uint16_t*)&packet[i]) = htons(len - ETHERNET_HEADER_LENGTH - IP_HEADER_LENGTH); i+=2; // ospf length (header + data)
	*((uint32_t*)&packet[i]) = htonl(subsystem->pwospf.routerID); i+=4; // router ID
	*((uint32_t*)&packet[i]) = htonl(subsystem->pwospf.areaID); i+=4; // area ID
	packet[i++] = 0; packet[i++] = 0; // checksum (calculated later)
	packet[i++] = 0; packet[i++] = 0; // Autype
	packet[i++] = 0; packet[i++] = 0; packet[i++] = 0; packet[i++] = 0; // Authentication #1
	packet[i++] = 0; packet[i++] = 0; packet[i++] = 0; packet[i++] = 0; // Authentication #2

	// LSU packet
	*((uint16_t*)&packet[i]) = htons(sequence); i+=2; // sequence
	*((uint16_t*)&packet[i]) = htons(LSU_DEFAULT_TTL); i+=2; // lsu ttl
	*((uint32_t*)&packet[i]) = htonl(advCnt); i+=4; // number of advertisements

	// add neighbor info to the packet
	iface = subsystem->pwospf.if_list;
	while(iface){
		if(isEnabled(iface->ip)){
			pthread_mutex_lock(&iface->neighbor_lock);
			struct pwospf_neighbor* nbor = iface->neighbor_list;
			while(nbor){
				*((uint32_t*)&packet[i]) = htonl(nbor->ip & iface->netmask); i+=4; // subnet
				*((uint32_t*)&packet[i]) = htonl(iface->netmask); i+=4; // mask
				*((uint32_t*)&packet[i]) = htonl(nbor->id); i+=4; // router ID				
				nbor = nbor->next;
			}				
			pthread_mutex_unlock(&iface->neighbor_lock);		
		}
			
		iface = iface->next;
	}
	
	// OSPF checksum (make sure Authentication fileds are set to 0 here)
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 12] = 0; // checksum
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 13] = 0; // checksum
	uint16_t ospfChksum = 
		checksum(	(uint16_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), 
					len - (ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH) );
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 12] = (htons(ospfChksum) >> 8) & 0xff; // OSPF checksum 
	packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 13] = (htons(ospfChksum) & 0xff); // OSPF checksum
	
	// send the packet out	
	for(i = 0; i < subsystem->num_ifaces; i++){
		if ( !(subsystem->ifaces[i].enabled) ){
			continue;
		}

		// update Ethernet Header
		uint8_t *myMAC = subsystem->ifaces[i].addr;
		for (k = 6, j = 0; k < 12; k++, j++) packet[k] = myMAC[j];	

		// src ip
		int2byteIP(subsystem->ifaces[i].ip, &packet[ETHERNET_HEADER_LENGTH + 12]); // source IP

		// IP checksum
		packet[ETHERNET_HEADER_LENGTH + 10] = 0; packet[ETHERNET_HEADER_LENGTH + 11] = 0; // checksum (calculated later)
		uint16_t ipChksum = checksum((uint16_t*)(&packet[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
		packet[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
		packet[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum
	
		// send packet
		sr_integ_low_level_output(sr, packet, len, subsystem->ifaces[i].name);	
	}

	sequence++;
	free(packet);
}

// sending a Hello packet out the interface with ifIP
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

	subsystem->pwospf.routerID = subsystem->ifaces[0].ip; // 0-th interface??? should it rather be eth0???
	subsystem->pwospf.areaID = AREA_ID; // this should be the same for all routers
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

// Find the interface struct given the ip
struct pwospf_if* findPWOSPFif(struct pwospf_router* router, uint32_t ip){
	struct pwospf_if* node = router->if_list;
	while(node){
		if(node->ip == ip) return node;
		node = node->next;
	}
	return NULL;
}

// finds a neighbor given its ip
// caller must hold lock on interface->neighbor_lock
struct pwospf_neighbor* findOSPFNeighbor(struct pwospf_if* interface, uint32_t ip){
	struct pwospf_neighbor* node = interface->neighbor_list;
	while(node){
		if(node->ip == ip) return node;
		node = node->next;
	}
	return NULL;
}