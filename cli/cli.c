/* Filename: cli.c */

#include <signal.h>
#include <stdio.h>               /* snprintf()                        */
#include <stdlib.h>              /* malloc()                          */
#include <string.h>              /* strncpy()                         */
#include <sys/time.h>            /* struct timeval                    */
#include <unistd.h>              /* sleep()                           */
#include "cli.h"
#include "cli_network.h"         /* make_thread()                     */
#include "helper.h"
#include "socket_helper.h"       /* writenstr()                       */
#include "../sr_base_internal.h" /* struct sr_instance                */
#include "../router.h"		/* interface enable/disable cli functions */
#include "../routingTable.h" /* routing table functions */
#include "../arpCache.h"	/* arp cache functions */
#include <time.h>

/* temporary */
#include "cli_stubs.h"

/** whether to shutdown the server or not */
static int router_shutdown;

/** socket file descriptor where responses should be sent */
static int fd;

/** whether the fd is was terminated */
static int fd_alive;

/** whether the client is in verbose mode */
static int* pverbose;

/** whether to skip next prompt call */
static int skip_next_prompt;

#ifdef _STANDALONE_CLI_
/**
 * Initialize sr for the standalone binary which just runs the CLI.
 */
struct sr_instance* my_get_sr() {
    static struct sr_instance* sr = NULL;
    if( ! sr ) {
        sr = malloc( sizeof(*sr) );
        true_or_die( sr!=NULL, "malloc falied in my_get_sr" );

        sr->interface_subsystem = NULL;
	struct sr_router *subsystem = (struct sr_router*)malloc(sizeof(sr_router));
	sr_set_subsystem(sr, subsystem);

        sr->topo_id = 0;
        strncpy( sr->vhost, "cli", SR_NAMELEN );
        strncpy( sr->user, "cli mode (no client)", SR_NAMELEN );
        if( gethostname(sr->lhost,  SR_NAMELEN) == -1 )
            strncpy( sr->lhost, "cli mode (unknown localhost)", SR_NAMELEN );

        /* NOTE: you probably want to set some dummy values for the rtable and
           interface list of your interface_subsystem here (preferably read them
           from a file) */
    }

    return sr;
}
#   define SR my_get_sr()
#else
#   include "../sr_integration.h" /* sr_get() */
#   define SR get_sr()
#endif

/**
 * Wrapper for writenstr.  Tries to send the specified string with the
 * file-scope fd.  If it fails, fd_alive is set to 0.  Does nothing if
 * fd_alive is already 0.
 */
static void cli_send_str( const char* str ) {
    if( fd_alive )
        if( 0 != writenstr( fd, str ) )
            fd_alive = 0;
}

#ifdef _VNS_MODE_
/**
 * Wrapper for writenstr.  Tries to send the specified string followed by a
 * newline with the file-scope fd.  If it fails, fd_alive is set to 0.  Does
 * nothing if fd_alive is already 0.
 */
static void cli_send_strln( const char* str ) {
    if( fd_alive )
        if( 0 != writenstrs( fd, 2, str, "\n" ) )
            fd_alive = 0;
}
#endif

/**
 * Wrapper for writenstrs.  Tries to send the specified string(s) with the
 * file-scope fd.  If it fails, fd_alive is set to 0.  Does nothing if
 * fd_alive is already 0.
 */
static void cli_send_strs( int num_args, ... ) {
    const char* str;
    int ret;
    va_list args;

    if( !fd_alive ) return;
    va_start( args, num_args );

    ret = 0;
    while( ret==0 && num_args-- > 0 ) {
        str = va_arg(args, const char*);
        ret = writenstr( fd, str );
    }

    va_end( args );
    if( ret != 0 )
        fd_alive = 0;
}

void cli_init() {
    router_shutdown = 0;
    skip_next_prompt = 0;
}

int cli_is_time_to_shutdown() {
    return router_shutdown;
}

int cli_focus_is_alive() {
    return fd_alive;
}

void cli_focus_set( const int sfd, int* verbose ) {
    fd_alive = 1;
    fd = sfd;
    pverbose = verbose;
}

void cli_send_help( cli_help_t help_type ) {
    if( fd_alive )
        if( !cli_send_help_to( fd, help_type ) )
            fd_alive = 0;
}

void cli_send_parse_error( int num_args, ... ) {
    const char* str;
    int ret;
    va_list args;

    if( fd_alive ) {
        va_start( args, num_args );

        ret = 0;
        while( ret==0 && num_args-- > 0 ) {
            str = va_arg(args, const char*);
            ret = writenstr( fd, str );
        }

        va_end( args );
        if( ret != 0 )
            fd_alive = 0;
    }
}

void cli_send_welcome() {
    cli_send_str( "You are now logged into the router CLI.\n" );
}

void cli_send_prompt() {
    if( !skip_next_prompt )
        cli_send_str( PROMPT );

    skip_next_prompt = 0;
}

void cli_show_all() {
#ifdef _CPUMODE_
    cli_show_hw();
    cli_send_str( "\n" );
#endif
    cli_show_ip();
#ifndef _CPUMODE_
#ifndef _MANUAL_MODE_
    cli_send_str( "\n" );
    cli_show_vns();
#endif
#endif
}

#ifndef _CPUMODE_
void cli_send_no_hw_str() {
    cli_send_str( "HW information is not available when not in CPU mode\n" );
}
#else
void cli_show_hw() {
    cli_send_str( "HW State:\n" );
    cli_show_hw_about();
    cli_show_hw_arp();
    cli_show_hw_intf();
    cli_show_hw_route();
}

void cli_show_hw_about() {
    char buf[STR_ARP_CACHE_MAX_LEN];
    router_hw_info_to_string( SR, buf, STR_HW_INFO_MAX_LEN );
    cli_send_str( buf );
}

void cli_show_hw_arp() {
    char buf[STR_ARP_CACHE_MAX_LEN];
    arp_cache_hw_to_string( SR, *pverbose, buf, STR_ARP_CACHE_MAX_LEN );
    cli_send_str( buf );
}

void cli_show_hw_intf() {
    char buf[STR_INTFS_HW_MAX_LEN];
    router_intf_hw_to_string( SR, buf, STR_INTFS_HW_MAX_LEN );
    cli_send_str( buf );
}

void cli_show_hw_route() {
    char buf[STR_RTABLE_MAX_LEN];
    rtable_hw_to_string( SR, *pverbose, buf, STR_RTABLE_MAX_LEN );
    cli_send_str( buf );
}
#endif

void cli_show_ip() {
    cli_send_str( "IP State:\n" );
    cli_show_ip_arp();
    cli_show_ip_intf();
    cli_show_ip_route();
}

void cli_show_ip_arp() {
    char buf[128];
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    pthread_mutex_lock(&list_lock);
    struct arpCacheNode *node = subsystem->arpList;
    uint8_t ip_str[4];
    while(node != NULL) {
		int2byteIP(node->ip, ip_str);
		sprintf(buf, "IP: %u.%u.%u.%u MAC: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x static:%d\n", 
			ip_str[0], ip_str[1], ip_str[2], ip_str[3],
			node->mac[0], node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5],
			node->is_static);
		node = node->next;
	    cli_send_str( buf );
	}
    pthread_mutex_unlock(&list_lock);
}

void cli_show_ip_intf() {
    char buf[128];
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    int i;
    
    pthread_rwlock_rdlock(&subsystem->if_lock);
    for(i = 0; i < subsystem->num_ifaces; i++) {
		struct sr_vns_if *node = &(subsystem->ifaces[i]);
		uint8_t ip_str[4];
		int2byteIP(node->ip, ip_str);
		sprintf(buf, "%s IP: %u.%u.%u.%u MAC: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x enabled:%d\n",
			node->name, 
			ip_str[0], ip_str[1], ip_str[2], ip_str[3],
			node->addr[0], node->addr[1], node->addr[2], node->addr[3], node->addr[4], node->addr[5],
			node->enabled);
	    cli_send_str( buf );
    }
    pthread_rwlock_unlock(&subsystem->if_lock);

}

void cli_show_ip_route() {
    char buf[128];
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
    rtableNode *node = subsystem->rtable;
    uint8_t ip[4], gw[4], nm[4];
    while(node != NULL) {
		int2byteIP(node->ip, ip);
		int2byteIP(node->gateway, gw);
		int2byteIP(node->netmask, nm);
		sprintf(buf, "IP:%d.%d.%d.%d  Gateway:%d.%d.%d.%d  Netmask:%d.%d.%d.%d  IF:%s Static:%d\n", 
			    ip[0], ip[1], ip[2], ip[3],
			    gw[0], gw[1], gw[2], gw[3],
			    nm[0], nm[1], nm[2], nm[3],
			    node->output_if, node->is_static);
		node = node->next;
	    cli_send_str( buf );
    }
}

void cli_show_opt() {
    cli_show_opt_verbose();
}

void cli_show_opt_verbose() {
    if( *pverbose )
        cli_send_str( "Verbose: Enabled\n" );
    else
        cli_send_str( "Verbose: Disabled\n" );
}

void cli_show_ospf() {
    cli_send_str( "Neighbor Information:\n" );
    cli_show_ospf_neighbors();

    cli_send_str( "Topology:\n" );
    cli_show_ospf_topo();
}

void cli_show_ospf_neighbors() {
    char buf[128];
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	struct pwospf_if *pw_if = subsystem->pwospf.if_list;
	
    uint8_t ip[4];

    while(pw_if != NULL) {
    	char* if_name = getIfName(pw_if->ip);
    	if(if_name){
			sprintf(buf, "%s: neighbors:\n", if_name);
			cli_send_str(buf);
			
			struct pwospf_neighbor *node = pw_if->neighbor_list;
			while(node){
				int2byteIP(node->ip, ip);
				sprintf(buf, "\tRouterID:%u  IP:%d.%d.%d.%d\n", 
					    node->id,
					    ip[0], ip[1], ip[2], ip[3]);
			    cli_send_str( buf );
			
				node = node->next;
			}
		}
		pw_if = pw_if->next;
    }
}

void cli_show_ospf_topo() {
    char buf[128];
	topo_router *rnode;
		
    uint8_t ip[4];

	pthread_mutex_lock(&topo_lock);
	rnode = topo_head;
	
	while(rnode){
		sprintf(buf, "RouterID:%u, last seq_num:%u, links:\n", rnode->router_id, rnode->last_seq);
		cli_send_str(buf);
		
		lsu_ad *lnode = rnode->ads;
		while(lnode){
			int2byteIP(lnode->subnet, ip);
			sprintf(buf, "\tRouterID:%u, subnet:%u.%u.%u.%u\n", lnode->router_id, ip[0], ip[1], ip[2], ip[3]);
			cli_send_str(buf);
			lnode = lnode->next;
		}
	
		rnode = rnode->next;
	}

	pthread_mutex_unlock(&topo_lock);

//    cli_send_str( "not yet implemented: show PWOSPF topology of SR (e.g., for each router, show its ID, last pwospf seq #, and a list of all its links (e.g., router ID + subnet))\n" );
}

#ifndef _VNS_MODE_
void cli_send_no_vns_str() {
#ifdef _CPUMODE_
    cli_send_str( "VNS information is not available when in CPU mode\n" );
#else
    cli_send_str( "VNS information is not available when in Manual mode\n" );
#endif
}
#else
void cli_show_vns() {
    cli_send_str( "VNS State:\n  Localhost: " );
    cli_show_vns_lhost();
    cli_send_str( "  Topology: " );
    cli_show_vns_topo();
    cli_send_str( "  User: " );
    cli_show_vns_user();
    cli_send_str( "  Virtual Host: " );
    cli_show_vns_vhost();
}

void cli_show_vns_lhost() {
    cli_send_strln( SR->lhost );
}

void cli_show_vns_topo() {
    char buf[7];
    snprintf( buf, 7, "%u\n", SR->topo_id );
    cli_send_str( buf );
}

void cli_show_vns_user() {
    cli_send_strln( SR->user );
}

void cli_show_vns_vhost() {
    cli_send_strln( SR->vhost );
}
#endif

void cli_manip_ip_arp_add( gross_arp_t* data ) {
    char ip[STRLEN_IP];
    char mac[STRLEN_MAC];

    ip_to_string( ip, data->ip );
    mac_to_string( mac, data->mac );

    if( arp_cache_static_entry_add( SR, data->ip, data->mac ) )
        cli_send_strs( 5, "Added translation of ", ip, " <-> ", mac, " to the static ARP cache\n" );
    else
        cli_send_strs( 5, "Error: Unable to add a translation of ", ip, " <-> ", mac,
                       " to the static ARP cache -- try removing another static entry first.\n" );
}

void cli_manip_ip_arp_del( gross_arp_t* data ) {
    char ip[STRLEN_IP];

    ip_to_string( ip, data->ip );
    if( arp_cache_static_entry_remove( SR, data->ip ) )
        cli_send_strs( 3, "Removed ", ip, " from the ARP cache\n" );
    else
        cli_send_strs( 3, "Error: ", ip, " was not a static ARP cache entry\n" );
}

void cli_manip_ip_arp_purge_all() {
    int countD, countS, countT;
    char str_countS[11];
    char str_countT[11];
    const char* whatS;
    const char* whatT;

    countD = arp_cache_dynamic_purge( SR );

    countS = arp_cache_static_purge( SR );
    whatS = ( countS == 1 ) ? " entry" : " entries";
    snprintf( str_countS, 11, "%u", countS );

    countT = countD + countS;
    whatT = ( countT == 1 ) ? " entry" : " entries";
    snprintf( str_countT, 11, "%u", countT );

    cli_send_strs( 8, "Removed ", str_countT, whatT,
                   " (", str_countS, " static", whatS, ") from the ARP cache\n" );
}

void cli_manip_ip_arp_purge_dyn() {
    int count;
    char str_count[11];
    const char* what;

    count = arp_cache_dynamic_purge( SR );
    what = ( count == 1 ) ? " entry" : " entries";
    snprintf( str_count, 11, "%u", count );
    cli_send_strs( 4, "Removed ", str_count, what, " from the ARP cache\n" );
}

void cli_manip_ip_arp_purge_sta() {
    int count;
    char str_count[11];
    const char* what;

    count = arp_cache_static_purge( SR );
    what = ( count == 1 ) ? " entry" : " entries";
    snprintf( str_count, 11, "%u", count );
    cli_send_strs( 5, "Removed ", str_count, " static", what, " from the ARP cache\n" );
}

void cli_manip_ip_intf_set( gross_intf_t* data ) {
    struct sr_vns_if *intf;
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(SR);

    intf = router_lookup_interface_via_name( SR, data->intf_name );
    if( intf ) {
    	pthread_rwlock_wrlock(&subsystem->if_lock);
    		struct pwospf_if *pw_if = findPWOSPFif(&subsystem->pwospf, intf->ip);
    		pw_if->ip = data->ip;
    		pw_if->netmask = data->subnet_mask;
			intf->ip = data->ip;
			intf->mask = data->subnet_mask;
    	pthread_rwlock_unlock(&subsystem->if_lock);
		
		cli_show_ip_intf();
		
		sendLSU();
        cli_send_strs( 2, data->intf_name, " updated\n" );
    }
    else
        cli_send_strs( 2, data->intf_name, " is not a valid interface\n" );
}

void cli_manip_ip_intf_set_enabled( const char* intf_name, int enabled ) {
    int ret;
    const char* what;

    ret = router_interface_set_enabled( SR, intf_name, enabled );
    what = (enabled ? "enabled\n" : "disabled\n");

    switch( ret ) {
    case 0:
        cli_send_strs( 3, intf_name, " has been ", what );
        break;

    case 1:
        cli_send_strs( 3, intf_name, " was already ", what );

    case -1:
    default:
        cli_send_strs( 2, intf_name, " is not a valid interface\n" );
    }
}

void cli_manip_ip_intf_down( gross_intf_t* data ) {
    cli_manip_ip_intf_set_enabled( data->intf_name, 0 );
}

void cli_manip_ip_intf_up( gross_intf_t* data ) {
    cli_manip_ip_intf_set_enabled( data->intf_name, 1 );
}

void cli_manip_ip_ospf_down() {
    if( router_is_ospf_enabled( SR ) ) {
        router_set_ospf_enabled( SR, 0 );
        cli_send_str( "OSPF has been disabled" );
    }
    else
        cli_send_str( "OSPF was already disabled" );
}

void cli_manip_ip_ospf_up() {
    if( !router_is_ospf_enabled( SR ) ) {
        router_set_ospf_enabled( SR, 1 );
        cli_send_str( "OSPF has been enabled" );
    }
    else
        cli_send_str( "OSPF was already enabled" );
}

void cli_manip_ip_route_add( gross_route_t* data ) {
    void *intf;
    intf = router_lookup_interface_via_name( SR, data->intf_name );
    if( !intf )
        cli_send_strs( 3, "Error: no interface with the name ",
                       data->intf_name, " exists.\n" );
    else {
        rtable_route_add(SR, data->dest, data->gw, data->mask, intf, 1);
        cli_send_str( "The route has been added.\n" );
    }
}

void cli_manip_ip_route_del( gross_route_t* data ) {
    if( rtable_route_remove( SR, data->dest, data->mask, 1 ) )
        cli_send_str( "The route has been removed.\n" );
    else
        cli_send_str( "That route does not exist.\n" );
}

void cli_manip_ip_route_purge_all() {
    rtable_purge_all( SR );
    cli_send_str( "All routes have been removed from the routing table.\n" );
}

void cli_manip_ip_route_purge_dyn() {
    rtable_purge( SR, 0 );
    cli_send_str( "All dymanic routes have been removed from the routing table.\n" );
}

void cli_manip_ip_route_purge_sta() {
    rtable_purge( SR, 1 );
    cli_send_str( "All static routes have been removed from the routing table.\n" );
}

void cli_date() {
    char str_time[STRLEN_TIME];
    struct timeval now;

    gettimeofday( &now, NULL );
    time_to_string( str_time, now.tv_sec );
    cli_send_str( str_time );
}

void cli_exit() {
    cli_send_str( "Goodbye!\n" );
    fd_alive = 0;
}

int cli_ping_handle_self( uint32_t ip ) {
    void* intf;

    intf = router_lookup_interface_via_ip( SR, ip );
    if( intf ) {
        if( router_is_interface_enabled( SR, intf ) )
            cli_send_str( "Your interface is up.\n" );
        else
            cli_send_str( "Your interface is down.\n" );

        return 1;
    }

    return 0;
}

/**
 * Sends a ping to the specified IP address.  Information about it being sent
 * and whether it succeeds or not should be sent to the specified client_fd.
 */
static void cli_send_ping( int client_fd, uint32_t ip ) {
    char buf[128];
    char *out_if = NULL;
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	out_if = lp_match(&(subsystem->rtable), ip); //output interface
	if(out_if == NULL) return;

	uint16_t identifier = rand() % 0xffff;
	uint16_t seqNum = 0;

	struct pingRequestNode* node = (struct pingRequestNode*)malloc(sizeof(struct pingRequestNode));
	node->fd = client_fd;
	node->identifier = identifier;
	node->seqNum = seqNum;
	node->pingIP = ip;
	node->lastTTL = 0;
	strcpy(node->interface, out_if);
	node->isTraceroute = 0;
	
	pthread_mutex_lock(&ping_lock);
		node->next = pingListHead;	
		pingListHead = node;
		sendICMPEchoRequest(out_if, ip, identifier, seqNum, &node->time, 64);
	pthread_mutex_unlock(&ping_lock);

	uint8_t ipStr[4];
	int2byteIP(ip, ipStr);
	sprintf(buf, "Ping request sent to %u.%u.%u.%u\n", ipStr[0], ipStr[1], ipStr[2], ipStr[3]);
	writenf(client_fd, buf);
	free(out_if);
}

void cli_ping( gross_ip_t* data ) {
    if( cli_ping_handle_self( data->ip ) )
        return;

    cli_send_ping(fd, data->ip );
    skip_next_prompt = 1;
}

void cli_ping_flood( gross_ip_int_t* data ) {
    int i;
    char str_ip[STRLEN_IP];

    if( cli_ping_handle_self( data->ip ) )
        return;

    ip_to_string( str_ip, data->ip );
    if( 0 != writenf( fd, "Will ping %s %u times ...\n", str_ip, data->count ) )
        fd_alive = 0;

    for( i=0; i<data->count; i++ )
        cli_send_ping( fd, data->ip );
    skip_next_prompt = 1;
}

void cli_shutdown() {
    cli_send_str( "Shutting down the router ...\n" );
    router_shutdown = 1;

    /* we could do a cleaner shutdown, but this is probably fine */
    exit(0);
}

void cli_traceroute( gross_ip_t* data ) {
    //cli_send_str( "not yet implemented: traceroute\n" );
    
    char buf[128];
    char *out_if = NULL;
    struct sr_instance* sr = get_sr();
    struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);

	out_if = lp_match(&(subsystem->rtable), data->ip); //output interface
	if(out_if == NULL) return;

	uint16_t identifier = rand() % 0xffff;
	uint16_t seqNum = 0;

	struct pingRequestNode* node = (struct pingRequestNode*)malloc(sizeof(struct pingRequestNode));
	node->fd = fd; // global fd
	node->identifier = identifier;
	node->seqNum = seqNum;
	node->pingIP = data->ip;
	node->lastTTL = 0;
	strcpy(node->interface, out_if);
	node->isTraceroute = 1;
	
	uint8_t ipStr[4];
	int2byteIP(data->ip, ipStr);
	sprintf(buf, "Traceroute to %u.%u.%u.%u\n", ipStr[0], ipStr[1], ipStr[2], ipStr[3]);
	writenf(fd, buf);

	pthread_mutex_lock(&ping_lock);
		node->next = pingListHead;	
		pingListHead = node;
		sendICMPEchoRequest(out_if, data->ip, identifier, seqNum, &node->time, 4);
	pthread_mutex_unlock(&ping_lock);

	free(out_if);
    
}

void cli_opt_verbose( gross_option_t* data ) {
    if( data->on ) {
        if( *pverbose )
            cli_send_str( "Verbose mode is already enabled.\n" );
        else {
            *pverbose = 1;
            cli_send_str( "Verbose mode is now enabled.\n" );
        }
    }
    else {
        if( *pverbose ) {
            *pverbose = 0;
            cli_send_str( "Verbose mode is now disabled.\n" );
        }
        else
            cli_send_str( "Verbose mode is already disabled.\n" );
    }
}
