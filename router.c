#include <stdlib.h>
#include <stdio.h>
#include "router.h"

enum packetType {IPv4, ARP};

void errorMsg(char* msg){
	fputs("error: ", stderr); fputs(msg, stderr); fputs("\n", stderr);
}

void dbgMsg(char* msg){
	fputs("dgb: ", stdout); fputs(msg, stdout); fputs("\n", stdout);
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
    	
    	}
    	else if (arpPacket[6] == 0 && arpPacket[7] == 2){
    		dbgMsg("ARP response received");
    	}
    	    
    }
 	       
}


uint8_t* generateARPresponse(const uint8_t * arpRequest, unsigned int len){



}
