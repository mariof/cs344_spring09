#include "router.h"

void processICMP(const char* interface, const uint8_t* packet, unsigned len){
	if(packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH] == 8){ // Echo Reqeust
		sendICMPEchoReply(interface, packet, len);
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

void sendICMPEchoReply(const char* interface, const uint8_t* requestPacket, unsigned len){
	int i;
	uint8_t *p = (uint8_t*)malloc(len*sizeof(uint8_t));
	uint32_t dstIP;
	
	if(len < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + ICMP_HEADER_LENGTH){
		errorMsg("Echo Request packet too short!");
		return;
	}
	
	dstIP = requestPacket[12] * 256 * 256 * 256 +
			requestPacket[13] * 256 * 256 +
			requestPacket[14] * 256 +
			requestPacket[15] * 1;    				
	dstIP = ntohl(dstIP);

//	for(i = 0; i < len; i++) printf("%d: %d\n", i, requestPacket[i]);
	
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
	int2byteIP(getInterfaceIP(interface), &p[i]); i += 4; // source IP
	int2byteIP(dstIP, &p[i]); i+=4; // destination IP
	
	// IP checksum
	int ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
	p[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
	p[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum
	
	// ICMP header
	i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH;
	p[i++] = 0; p[i++] = 0; // Echo Reply
	p[i++] = 0; p[i++] = 0; // ICMP checksum (calculated later)	
		
	// ICMP data
	for(i = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + ICMP_HEADER_LENGTH; i < len; i++) p[i] = requestPacket[i];

	// ICMP checksum
	ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), len - (ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH));
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 2] = (htons(ipChksum) >> 8) & 0xff; // ICMP checksum 
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 3] = (htons(ipChksum) & 0xff); // ICMP checksum
	
	// send the packet out
	sendIPpacket(get_sr(), interface, getNextHopIP(dstIP), p, len);
	
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
void sendICMPDestinationUnreachable(const char* interface, uint8_t* originalPacket, unsigned len, int code){
	int i, j;
	uint8_t *p = (uint8_t*)malloc(60*sizeof(uint8_t));
	uint32_t dstIP;

	if(len < ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 8){
		errorMsg("Original packet too short!");
		return;
	}
	
	dstIP = originalPacket[12] * 256 * 256 * 256 +
			originalPacket[13] * 256 * 256 +
			originalPacket[14] * 256 +
			originalPacket[15] * 1;    				
	dstIP = ntohl(dstIP);

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
	int ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
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
	ipChksum = checksum((uint16_t*)(&p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH]), 8+28);
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 2] = (htons(ipChksum) >> 8) & 0xff; // ICMP checksum 
	p[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + 3] = (htons(ipChksum) & 0xff); // ICMP checksum
	
	// send the packet out
	sendIPpacket(get_sr(), interface, getNextHopIP(dstIP), p, len);
	
	free(p);


}

void sendICMPTimeExceeded(uint32_t dstIP, const char* interface){

}
