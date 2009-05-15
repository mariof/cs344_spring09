#include "router.h"
#include "cli/socket_helper.h"
#include <sys/time.h>

void processICMP(const char* interface, const uint8_t* packet, unsigned len){
	if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH] == 8){ // Echo Reqeust
		sendICMPEchoReply(interface, packet, len);
	}
	else if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH] == 0){ // Echo Reply
		processEchoReply(packet, len);
	}
	else if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH] == 11){ // TTL expired
		processTTLexpired(packet, len);
	}
}

// calculates time difference in miliseconds
inline double deltaTimeMili(struct timeval *t1, struct timeval *t2){
	return (double)abs((t1->tv_sec - t2->tv_sec) * 1e6 + (t1->tv_usec - t2->tv_usec) ) / (double)1e3;
}

// match incoming ping reply with set request
void processEchoReply(const uint8_t* packet, unsigned len){
	uint16_t identifier, seqNum;
	struct timeval tv;
	gettimeofday(&tv, 0);
	
	identifier = ntohs(*((uint16_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 4])));
	seqNum = ntohs(*((uint16_t*)(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 6])));
	
	pthread_mutex_lock(&ping_lock);
		struct pingRequestNode *node = pingListHead;
		struct pingRequestNode *prev = NULL;
		while(node){
			if(node->identifier == identifier){
				if(node->isTraceroute == 0){
					writenf(node->fd, "Ping Reply from: %u.%u.%u.%u icmp_seq=%u ttl=%u time=%.3fms\n",
								packet[ETHERNET_HEADER_LENGTH + 12], packet[ETHERNET_HEADER_LENGTH + 13],
								packet[ETHERNET_HEADER_LENGTH + 14], packet[ETHERNET_HEADER_LENGTH + 15],
								seqNum, packet[ETHERNET_HEADER_LENGTH + 8], 
								deltaTimeMili(&tv, &node->time));
				}
				else{
					writenf(node->fd, "%u   %u.%u.%u.%u   time=%.3fms\n", node->lastTTL+1,
								packet[ETHERNET_HEADER_LENGTH + 12], packet[ETHERNET_HEADER_LENGTH + 13],
								packet[ETHERNET_HEADER_LENGTH + 14], packet[ETHERNET_HEADER_LENGTH + 15], 
								deltaTimeMili(&tv, &node->time));
					writenf(node->fd, "Traceroute compeleted.\n");
				}
				if(prev){
					prev->next = node->next;
				}
				else{
					pingListHead = node->next;
				}
				free(node);
				break;
			}
			prev = node;
			node = node->next;
		}
	pthread_mutex_unlock(&ping_lock);
}

// match TTL expired with a possible traceroute in progress
void processTTLexpired(const uint8_t* packet, unsigned len){
	uint16_t identifier, seqNum;
	uint8_t sentTTL;
	struct timeval tv;
	gettimeofday(&tv, 0);
	const int orig_ip_header_offset = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 8;
	
	identifier = ntohs(*((uint16_t*)(&packet[orig_ip_header_offset + IP_HEADER_LENGTH + 4])));
	seqNum = ntohs(*((uint16_t*)(&packet[orig_ip_header_offset + IP_HEADER_LENGTH + 6])));
	sentTTL = packet[orig_ip_header_offset + 8];
	
	pthread_mutex_lock(&ping_lock);
		struct pingRequestNode *node = pingListHead;
		struct pingRequestNode *prev = NULL;
		while(node){
			if(node->identifier == identifier && node->isTraceroute == 1){
				writenf(node->fd, "%u   %u.%u.%u.%u   time=%.3fms\n", node->lastTTL+1,
							packet[ETHERNET_HEADER_LENGTH + 12], packet[ETHERNET_HEADER_LENGTH + 13],
							packet[ETHERNET_HEADER_LENGTH + 14], packet[ETHERNET_HEADER_LENGTH + 15], 
							deltaTimeMili(&tv, &node->time));
							
				node->seqNum++;			
				node->lastTTL++;
				sendICMPEchoRequest(node->interface, node->pingIP, node->identifier, node->seqNum, &node->time, node->lastTTL+1);
										
				break;
			}
			prev = node;
			node = node->next;
		}
	pthread_mutex_unlock(&ping_lock);
}

// maintain the ping list
void refreshPingList(void *dummy){	
	while(1){
		pthread_mutex_lock(&ping_lock);
			struct timeval tv;
			gettimeofday(&tv, 0);
			struct pingRequestNode *node = pingListHead;
			struct pingRequestNode *prev = NULL;
			while(node){
				if(deltaTimeMili(&tv, &node->time) > (PING_LIST_TIMEOUT * 1000)){
					uint8_t strIP[4];
					int2byteIP(node->pingIP, strIP);
					if(node->isTraceroute == 0){
						writenf(node->fd, "No Ping Reply from: %u.%u.%u.%u\n", strIP[0], strIP[1], strIP[2], strIP[3]);
					}
					else{
						writenf(node->fd, "Traceroute to: %u.%u.%u.%u aborted.\n", strIP[0], strIP[1], strIP[2], strIP[3]);
					}
					if(prev){
						prev->next = node->next;
					}
					else{
						pingListHead = node->next;
					}
					free(node);
					break;
				}
				prev = node;
				node = node->next;
			}
		pthread_mutex_unlock(&ping_lock);	
		sleep(PING_LIST_REFRESH);
	}
}


// returns 16 bit checksum for IP and ICMP headers
uint16_t checksum(uint16_t* data, unsigned len){
	uint32_t acc;
	
	for(acc = 0; len > 0; len -= 2){
		acc += *data++;
	}
	
	while(acc >> 16){
		acc = (acc & 0xffff) + (acc >> 16);
	}
	return ~(acc & 0xffff);
}

// sends out Ping Response
void sendICMPEchoReply(const char* interface, const uint8_t* requestPacket, unsigned len){
	int i;
	uint8_t *p = (uint8_t*)malloc(len*sizeof(uint8_t));
	uint32_t dstIP;
	
	if(len < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + ICMP_HEADER_LENGTH){
		errorMsg("Echo Request packet too short!");
		return;
	}
	
	dstIP = requestPacket[ETHERNET_HEADER_LENGTH + 12] * 256 * 256 * 256 +
			requestPacket[ETHERNET_HEADER_LENGTH + 13] * 256 * 256 +
			requestPacket[ETHERNET_HEADER_LENGTH + 14] * 256 +
			requestPacket[ETHERNET_HEADER_LENGTH + 15] * 1;    				

	//for(i = 0; i < len; i++) printf("%d: %d\n", i, requestPacket[i]);
	
	// Ethernet header
	for(i = 0; i < ETHERNET_HEADER_LENGTH; i++) p[i] = 0;
	
	// IP header
	i = ETHERNET_HEADER_LENGTH;
	p[i++] = 69; // version and length 
	p[i++] = 0; // TOS
	p[i] = requestPacket[i]; i++; // total length (same as request)
	p[i] = requestPacket[i]; i++; // total length (same as request)
	p[i++] = (uint8_t)(rand() % 256); p[i++] = (uint8_t)(rand() % 256); // identification
	p[i++] = 0; p[i++] = 0; // fragmentation
	p[i++] = 64; // TTL
	p[i++] = 1; // protocol (ICMP)
	p[i++] = 0; p[i++] = 0; // checksum (calculated later)
	//int2byteIP(getInterfaceIP(interface), &p[i]); i += 4; // source IP
	memcpy(&p[i], &requestPacket[ETHERNET_HEADER_LENGTH + 16], 4); i+=4; // source IP
	int2byteIP(dstIP, &p[i]); i+=4; // destination IP
	
	// IP checksum
	uint16_t ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
	p[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
	p[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum
	
	// ICMP header
	i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH;
	p[i++] = 0; p[i++] = 0; // Echo Reply
	p[i++] = 0; p[i++] = 0; // ICMP checksum (calculated later)	
		
	// ICMP data
	for(i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + ICMP_HEADER_LENGTH; i < len; i++) p[i] = requestPacket[i];

	// ICMP checksum
	uint16_t icmpChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), len - (ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH));
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 2] = (htons(icmpChksum) >> 8) & 0xff; // ICMP checksum 
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 3] = (htons(icmpChksum) & 0xff); // ICMP checksum
	
	// send the packet out
	sendIPpacket(get_sr(), interface, getNextHopIP(dstIP), p, len);
	
//	for(i = 0; i < len; i++) printf("%d: %d\n", i, p[i]);
		
	free(p);
}

// sends out Ping Request with 56 byte payload
void sendICMPEchoRequest(const char* interface, uint32_t dstIP, uint16_t identifier, uint16_t seqNum, struct timeval* time, uint8_t ttl){ // dstIP, identifier, seqNum in host byte order
	int i;
	int len = 98; // 56 + 8 + 20 + 14
	uint8_t *p = (uint8_t*)malloc(len*sizeof(uint8_t));
		
	// Ethernet header
	for(i = 0; i < ETHERNET_HEADER_LENGTH; i++) p[i] = 0;
	
	// IP header
	i = ETHERNET_HEADER_LENGTH;
	p[i++] = 69; // version and length 
	p[i++] = 0; // TOS
	p[i++] = 0; // total length 
	p[i++] = 84; // total length (56+8+20)
	p[i++] = (uint8_t)(rand() % 256); p[i++] = (uint8_t)(rand() % 256); // identification
	p[i++] = 0; p[i++] = 0; // fragmentation
	p[i++] = ttl; // TTL
	p[i++] = 1; // protocol (ICMP)
	p[i++] = 0; p[i++] = 0; // checksum (calculated later)
	int2byteIP(getInterfaceIP(interface), &p[i]); i += 4; // source IP
	int2byteIP(dstIP, &p[i]); i+=4; // destination IP
	
	// IP checksum
	uint16_t ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
	p[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
	p[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum
	
	// ICMP header
	i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH;
	p[i++] = 8; p[i++] = 0; // Echo Request
	p[i++] = 0; p[i++] = 0; // ICMP checksum (calculated later)
	p[i++] = identifier / 256; p[i++] = identifier % 256;
	p[i++] = seqNum / 256; p[i++] = seqNum % 256;
		
	// ICMP data
	uint8_t num;
	for(i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + ICMP_HEADER_LENGTH + 4, num = 8; i < len; i++, num++) p[i] = num;

	// ICMP checksum
	uint16_t icmpChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), len - (ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH));
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 2] = (htons(icmpChksum) >> 8) & 0xff; // ICMP checksum 
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 3] = (htons(icmpChksum) & 0xff); // ICMP checksum
	
	// send the packet out
	dbgMsg("ICMP: Sending Echo Request");
	gettimeofday(time, 0);
	sendIPpacket(get_sr(), interface, getNextHopIP(dstIP), p, len);
	
//	for(i = 0; i < len; i++) printf("%d: %d\n", i, p[i]);
		
	free(p);
}


// sends ICMP destination unreachable error with code:
/*
0	Network unreachable error.
1	Host unreachable error.
2	Protocol unreachable error. When the designated transport protocol is not supported.
3	Port unreachable error. When the designated transport protocol (e.g., UDP) is unable to demultiplex the datagram but has no protocol mechanism to inform the sender.
4	The datagram is too big. Packet fragmentation is required but the DF bit in the IP header is set.
5	Source route failed error.
6	Destination network unknown error.
7	Destination host unknown error.
8	Source host isolated error. Obsolete.
9	The destination network is administratively prohibited.
10	The destination host is administratively prohibited.
11	The network is unreachable for Type Of Service.
12	The host is unreachable for Type Of Service.
13	Communication Administratively Prohibited. This is generated if a router cannot forward a packet due to administrative filtering.
14	Host precedence violation. Sent by the first hop router to a host to indicate that a requested precedence is not permitted for the particular combination of source/destination host or network, upper layer protocol, and source/destination port.
15	Precedence cutoff in effect. The network operators have imposed a minimum level of precedence required for operation, the datagram was sent with a precedence below this level.
*/
void sendICMPDestinationUnreachable(const char* interface, const uint8_t* originalPacket, unsigned len, int code){
	int i, j;
	int myLen = 70;
	uint8_t *p = (uint8_t*)malloc(myLen*sizeof(uint8_t));
	uint32_t dstIP;

	if(len < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 8){
		errorMsg("Original packet too short!");
		return;
	}
	
	dstIP = originalPacket[ETHERNET_HEADER_LENGTH + 12] * 256 * 256 * 256 +
			originalPacket[ETHERNET_HEADER_LENGTH + 13] * 256 * 256 +
			originalPacket[ETHERNET_HEADER_LENGTH + 14] * 256 +
			originalPacket[ETHERNET_HEADER_LENGTH + 15] * 1;    				

//	for(i = 0; i < len; i++) printf("%d: %d\n", i, requestPacket[i]);
	
	// Ethernet header
	for(i = 0; i < ETHERNET_HEADER_LENGTH; i++) p[i] = 0;
	
	// IP header
	i = ETHERNET_HEADER_LENGTH;
	p[i++] = 69; // version and length 
	p[i++] = 0; // TOS
	p[i++] = 0; p[i++] = 56;// total length
	p[i++] = (uint8_t)(rand() % 256); p[i++] = (uint8_t)(rand() % 256); // identification
	p[i++] = 0; p[i++] = 0; // fragmentation
	p[i++] = 64; // TTL
	p[i++] = 1; // protocol (ICMP)
	p[i++] = 0; p[i++] = 0; // checksum (calculated later)
	int2byteIP(getInterfaceIP(interface), &p[i]); i += 4; // source IP
	int2byteIP(dstIP, &p[i]); i+=4; // destination IP
	
	// IP checksum
	uint16_t ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
	p[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
	p[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum
	
	// ICMP header
	i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH;
	p[i++] = 3; p[i++] = code; // Unreachable
	p[i++] = 0; p[i++] = 0; // ICMP checksum (calculated later)	
	p[i++] = 0; p[i++] = 0; // unused
	p[i++] = (htons(1500) >> 8) & 0xff; // next hop MTU
	p[i++] = htons(1500) & 0xff; // next hop MTU
		
	// ICMP data
	for(j = ETHERNET_HEADER_LENGTH; j < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 8; i++, j++) p[i] = originalPacket[j];

	// ICMP checksum
	uint16_t icmpChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), 8+28);
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 2] = (htons(icmpChksum) >> 8) & 0xff; // ICMP checksum 
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 3] = (htons(icmpChksum) & 0xff); // ICMP checksum
	
	// send the packet out
	dbgMsg("ICMP: Sending destination unreachable");
	sendIPpacket(get_sr(), interface, getNextHopIP(dstIP), p, myLen);
	free(p);
}

// sends out a TTL expired ICMP message
void sendICMPTimeExceeded(const char* interface, const uint8_t* originalPacket, unsigned len){
	int i, j;
	int myLen = 70; 
	uint8_t *p = (uint8_t*)malloc(myLen*sizeof(uint8_t));
	uint32_t dstIP;

	if(len < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 8){
		errorMsg("Original packet too short!");
		return;
	}
	
	dstIP = originalPacket[ETHERNET_HEADER_LENGTH + 12] * 256 * 256 * 256 +
			originalPacket[ETHERNET_HEADER_LENGTH + 13] * 256 * 256 +
			originalPacket[ETHERNET_HEADER_LENGTH + 14] * 256 +
			originalPacket[ETHERNET_HEADER_LENGTH + 15] * 1;    				

//	for(i = 0; i < len; i++) printf("%d: %d\n", i, requestPacket[i]);
	
	// Ethernet header
	for(i = 0; i < ETHERNET_HEADER_LENGTH; i++) p[i] = 0;
	
	// IP header
	i = ETHERNET_HEADER_LENGTH;
	p[i++] = 69; // version and length 
	p[i++] = 0; // TOS
	p[i++] = 0; p[i++] = 56;// total length
	p[i++] = (uint8_t)(rand() % 256); p[i++] = (uint8_t)(rand() % 256); // identification
	p[i++] = 0; p[i++] = 0; // fragmentation
	p[i++] = 64; // TTL
	p[i++] = 1; // protocol (ICMP)
	p[i++] = 0; p[i++] = 0; // checksum (calculated later)
	int2byteIP(getInterfaceIP(interface), &p[i]); i += 4; // source IP
	int2byteIP(dstIP, &p[i]); i+=4; // destination IP
	
	// IP checksum
	uint16_t ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
	p[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
	p[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum
	
	// ICMP header
	i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH;
	p[i++] = 11; p[i++] = 0; // TTL expired
	p[i++] = 0; p[i++] = 0; // ICMP checksum (calculated later)	
	p[i++] = 0; p[i++] = 0; p[i++] = 0; p[i++] = 0;// unused
		
	// ICMP data
	for(j = ETHERNET_HEADER_LENGTH; j < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 8; i++, j++) p[i] = originalPacket[j];

	// ICMP checksum
	uint16_t icmpChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), 8+28);
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 2] = (htons(icmpChksum) >> 8) & 0xff; // ICMP checksum 
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 3] = (htons(icmpChksum) & 0xff); // ICMP checksum
	
	// send the packet out
	dbgMsg("ICMP: Sending TTL Exceeded");
	sendIPpacket(get_sr(), interface, getNextHopIP(dstIP), p, myLen);
	free(p);
}
