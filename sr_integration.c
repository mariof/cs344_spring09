/*-----------------------------------------------------------------------------
 * file:  sr_integration.c
 * date:  Tue Feb 03 11:29:17 PST 2004
 * Author: Martin Casado <casado@stanford.edu>
 *
 * Description:
 *
 * Methods called by the lowest-level of the network system to talk with
 * the network subsystem.
 *
 * This is the entry point of integration for the network layer.
 *
 *---------------------------------------------------------------------------*/

#include <stdlib.h>

#include <assert.h>
#include <string.h>

#include "sr_vns.h"
#include "sr_base_internal.h"

#include "router.h"

#include "lwtcp/lwip/sys.h"

#ifdef _CPUMODE_
#include "sr_cpu_extension_nf2.h"
#endif


/*-----------------------------------------------------------------------------
 * Method: sr_integ_init(..)
 * Scope: global
 *
 *
 * First method called during router initialization.  Called before connecting
 * to VNS, reading in hardware information etc.
 *
 *---------------------------------------------------------------------------*/
void sr_integ_init(struct sr_instance* sr)
{
    printf(" ** sr_integ_init(..) called \n");

    struct sr_router* subsystem = (struct sr_router*)malloc(sizeof(struct sr_router));
 	assert(subsystem);
 	subsystem->num_ifaces = 0;
 	subsystem->ifaces = NULL;
    sr_set_subsystem(sr, subsystem);
    
    pthread_rwlock_init(&tree_lock, NULL);
    pthread_mutex_init(&list_lock, NULL);
    pthread_mutex_init(&queue_lock, NULL);
    pthread_mutex_init(&rtable_lock, NULL);
    pthread_mutex_init(&ping_lock, NULL);
    
    subsystem->arpQueue = NULL;
    subsystem->arpList = NULL;
    subsystem->arpTree = NULL;
    subsystem->rtable = NULL;

	pingListHead = NULL;

	srand(time(NULL));

} /* -- sr_integ_init -- */

/*-----------------------------------------------------------------------------
 * Method: sr_integ_hw_setup(..)
 * Scope: global
 *
 * Called after all initial hardware information (interfaces) have been
 * received.  Can be used to start subprocesses (such as dynamic-routing
 * protocol) which require interface information during initialization.
 *
 *---------------------------------------------------------------------------*/

void sr_integ_hw_setup(struct sr_instance* sr)
{
    printf(" ** sr_integ_hw(..) called \n");
    
    // start refresh threads
	sys_thread_new(arpCacheRefresh, NULL);
	sys_thread_new(arpQueueRefresh, NULL);
	sys_thread_new(refreshPingList, NULL);
	sys_thread_new(topologyRefresh, NULL);
	    
//    if( pthread_create(&arpCacheRefreshThread, NULL, arpCacheRefresh, NULL) == 0 )
//    	pthread_detach(arpCacheRefreshThread);
    
//    if( pthread_create(&arpQueueRefreshThread, NULL, arpQueueRefresh, NULL) == 0 )
//    	pthread_detach(arpQueueRefreshThread);

    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    // Load routing table
    fill_rtable(&(subsystem->rtable));
    
    // init pwospf
    initPWOSPF(sr);

	// start pwospf threads
	struct pwospf_if* node = subsystem->pwospf.if_list;
	while(node){
		sys_thread_new(pwospfSendHelloThread, (void*)node);
		node = node->next;
	}
	sys_thread_new(pwospfSendLSUThread, NULL);

	// Load thread pool system
	initThreadPool();

    //testList(sr);
    
} /* -- sr_integ_hw_setup -- */

/*---------------------------------------------------------------------
 * Method: sr_integ_input(struct sr_instance*,
 *                        uint8_t* packet,
 *                        char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_integ_input(struct sr_instance* sr,
        const uint8_t * packet/* borrowed */,
        unsigned int len,
        const char* interface/* borrowed */)
{
    /* -- INTEGRATION PACKET ENTRY POINT!-- */
	
//    printf(" ** sr_integ_input(..) called \n");
    
//	processPacket(sr, packet, len, interface);		
	addThreadQueue(sr, packet, len, interface);

} /* -- sr_integ_input -- */

/*-----------------------------------------------------------------------------
 * Method: sr_integ_add_interface(..)
 * Scope: global
 *
 * Called for each interface read in during hardware initialization.
 * struct sr_vns_if is defined in sr_base_internal.h
 *
 *---------------------------------------------------------------------------*/

void sr_integ_add_interface(struct sr_instance* sr,
                            struct sr_vns_if* vns_if/* borrowed */)
{
	int i;
	
    printf(" ** sr_integ_add_interface(..) called \n");
    
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    subsystem->num_ifaces++;
    subsystem->ifaces = (struct sr_vns_if*)realloc(subsystem->ifaces, subsystem->num_ifaces*sizeof(struct sr_vns_if));
    
    struct sr_vns_if *tmp_if = &subsystem->ifaces[subsystem->num_ifaces-1];
    strcpy(tmp_if->name, vns_if->name);
    for(i = 0; i < 6; i++) tmp_if->addr[i] = vns_if->addr[i];
    tmp_if->ip = vns_if->ip;
    tmp_if->mask = vns_if->mask;
    tmp_if->speed = vns_if->speed;
    tmp_if->enabled = 1;
        
    //printf("ip: %u\n", vns_if->ip);
        
        
} /* -- sr_integ_add_interface -- */

struct sr_instance* get_sr() {
    struct sr_instance* sr;

    sr = sr_get_global_instance( NULL );
    assert( sr );
    return sr;
}

/*-----------------------------------------------------------------------------
 * Method: sr_integ_low_level_output(..)
 * Scope: global
 *
 * Send a packet to VNS to be injected into the topology
 *
 *---------------------------------------------------------------------------*/

int sr_integ_low_level_output(struct sr_instance* sr /* borrowed */,
                             uint8_t* buf /* borrowed */ ,
                             unsigned int len,
                             const char* iface /* borrowed */)
{
#ifdef _CPUMODE_
    return sr_cpu_output(sr, buf /*lent*/, len, iface);
#else
    return sr_vns_send_packet(sr, buf /*lent*/, len, iface);
#endif /* _CPUMODE_ */
} /* -- sr_vns_integ_output -- */

/*-----------------------------------------------------------------------------
 * Method: sr_integ_destroy(..)
 * Scope: global
 *
 * For memory deallocation pruposes on shutdown.
 *
 *---------------------------------------------------------------------------*/

void sr_integ_destroy(struct sr_instance* sr)
{
    printf(" ** sr_integ_destroy(..) called \n");
    
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

    /* free routing table */
    rtableNode *node = subsystem->rtable;
    while(node != NULL) {
	rtableNode *next_node = node->next;
	free(node);
	node = next_node;
    }

    free(subsystem->ifaces);
    free(subsystem);
    
    destroyThreadPool();
    
    pthread_rwlock_destroy(&tree_lock);
    pthread_mutex_destroy(&list_lock);
    pthread_mutex_destroy(&queue_lock);
    pthread_mutex_destroy(&rtable_lock);
    pthread_mutex_destroy(&ping_lock);
    
} /* -- sr_integ_destroy -- */

/*-----------------------------------------------------------------------------
 * Method: sr_integ_findsrcip(..)
 * Scope: global
 *
 * Called by the transport layer for outgoing packets generated by the
 * router.  Expects source address in network byte order.
 *
 *---------------------------------------------------------------------------*/

uint32_t sr_integ_findsrcip(uint32_t dest /* nbo */)
{
//    fprintf(stderr, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
//    fprintf(stderr, "!!! Tranport layer called ip_findsrcip(..) this must be !!!\n");
//    fprintf(stderr, "!!! defined to return the correct source address        !!!\n");
//    fprintf(stderr, "!!! given a destination                                 !!!\n ");
//    fprintf(stderr, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	printf("call to sr_integ_findsrcip\n");
    /* --
     * e.g.
     *
     * struct sr_instance* sr = sr_get_global_instance();
     * struct my_router* mr = (struct my_router*)
     *                              sr_get_subsystem(sr);
     * return my_findsrcip(mr, dest);
     * -- */
    struct sr_instance* sr = sr_get_global_instance(NULL);
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

    // Get source interface from the routing table
    char *src_if = lp_match(&(subsystem->rtable), ntohl(dest));

    // Get IP address of the if from the sr_router struct
    if(src_if == NULL)
		return 0;

    int i;
    for(i = 0; i < subsystem->num_ifaces; i++){
		if (!strcmp(src_if, subsystem->ifaces[i].name)) {
		    // Convert IP address to network order and return
		    return htonl(subsystem->ifaces[i].ip);
		}
    }

    return 0;
} /* -- ip_findsrcip -- */

/*-----------------------------------------------------------------------------
 * Method: sr_integ_ip_output(..)
 * Scope: global
 *
 * Called by the transport layer for outgoing packets that need IP
 * encapsulation.
 *
 *---------------------------------------------------------------------------*/

uint32_t sr_integ_ip_output(uint8_t* payload /* given */,
                            uint8_t  proto,
                            uint32_t src, /* nbo */ // liar, it's hbo (mariof)
                            uint32_t dest, /* nbo */ // liar, it's hbo (mariof)
                            int len)
{
//    fprintf(stderr, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
//    fprintf(stderr, "!!! Tranport layer called sr_integ_ip_output(..)        !!!\n");
//    fprintf(stderr, "!!! this must be defined to handle the network          !!!\n ");
//    fprintf(stderr, "!!! level functionality of transport packets            !!!\n ");
//    fprintf(stderr, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

//    assert(0);
//	printf("call to sr_integ_ip_output\n");

	int i, myLen;
	struct sr_instance* sr = get_sr();
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	char* interface = NULL;
	
	myLen = ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH + len;
	if(myLen < 60) myLen = 60;
	
	uint8_t *packet = (uint8_t*)malloc(myLen*sizeof(uint8_t));
	memset(packet, 0, myLen);
	memcpy(&packet[ETHERNET_HEADER_LENGTH + IP_HEADER_LENGTH], payload, len);
	free(payload);
	
//	uint8_t ss[4], dd[4];
//	int2byteIP(src, ss);
//	int2byteIP(dest, dd);	
//	printf("len: %d src: %u.%u.%u.%u   dest: %u.%u.%u.%u\n", len, ss[0], ss[1], ss[2], ss[3], dd[0], dd[1], dd[2], dd[3]);
		
	interface = lp_match(&(subsystem->rtable), dest); //output interface
	
	if(interface == NULL){
		errorMsg("Unknown interface");
		return 1;
	}
		
	// IP header
	i = ETHERNET_HEADER_LENGTH;
	packet[i++] = 69; // version and header length 
	packet[i++] = 0; // TOS
	packet[i++] = 0; packet[i++] = IP_HEADER_LENGTH + len;// total length
	packet[i++] = (uint8_t)(rand() % 256); packet[i++] = (uint8_t)(rand() % 256); // identification
	packet[i++] = 0; packet[i++] = 0; // fragmentation
	packet[i++] = 64; // TTL
	packet[i++] = proto; // protocol
	packet[i++] = 0; packet[i++] = 0; // checksum (calculated later)
	int2byteIP(src, &packet[i]); i += 4; // source IP
	int2byteIP(dest, &packet[i]); i += 4; // destination IP
	
	// IP checksum
	int ipChksum = checksum((uint16_t*)(&packet[ETHERNET_HEADER_LENGTH]), IP_HEADER_LENGTH);
	packet[ETHERNET_HEADER_LENGTH + 10] = (htons(ipChksum) >> 8) & 0xff; // IP checksum 
	packet[ETHERNET_HEADER_LENGTH + 11] = (htons(ipChksum) & 0xff); // IP checksum	
	
	sendIPpacket(sr, interface, getNextHopIP(dest), packet, myLen);
	free(packet);
	free(interface);

    /* --
     * e.g.
     *
     * struct sr_instance* sr = sr_get_global_instance();
     * struct my_router* mr = (struct my_router*)
     *                              sr_get_subsystem(sr);
     * return my_ip_output(mr, payload, proto, src, dest, len);
     * -- */

    return 0;
} /* -- ip_integ_route -- */

/*-----------------------------------------------------------------------------
 * Method: sr_integ_close(..)
 * Scope: global
 *
 *  Called when the router is closing connection to VNS.
 *
 *---------------------------------------------------------------------------*/

void sr_integ_close(struct sr_instance* sr)
{
    printf(" ** sr_integ_close(..) called \n");
}  /* -- sr_integ_close -- */
