/*-----------------------------------------------------------------------------
 * file:  sr_cpu_extension_nf2.c
 * date:  Mon Feb 09 16:58:30 PST 2004
 * Author: Martin Casado
 *
 * 2007-Apr-04 04:57:55 AM - Modified to support NetFPGA v2.1 /mc
 *
 * Description:
 *
 *---------------------------------------------------------------------------*/

#include "sr_cpu_extension_nf2.h"

#include "sr_base_internal.h"

#include "sr_vns.h"

#include "router.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <sys/ioctl.h>
#include <linux/netdevice.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <fcntl.h>

struct sr_ethernet_hdr
{
#ifndef ETHER_ADDR_LEN
#define ETHER_ADDR_LEN 6
#endif
    uint8_t  ether_dhost[ETHER_ADDR_LEN];    /* destination ethernet address */
    uint8_t  ether_shost[ETHER_ADDR_LEN];    /* source ethernet address */
    uint16_t ether_type;                     /* packet type ID */
} __attribute__ ((packed)) ;

static char*    copy_next_field(FILE* fp, char*  line, char* buf);
static uint32_t asci_to_nboip(const char* ip);
static void     asci_to_ether(const char* addr, uint8_t mac[6]);


/*-----------------------------------------------------------------------------
 * Method: sr_cpu_init_hardware(..)
 * scope: global
 *
 * Read information for each of the router's interfaces from hwfile
 *
 * format of the file is (1 interface per line)
 *
 * <name ip mask hwaddr>
 *
 * e.g.
 *
 * eth0 192.168.123.10 255.255.255.0 ca:fe:de:ad:be:ef
 *
 *---------------------------------------------------------------------------*/


int sr_cpu_init_hardware(struct sr_instance* sr, const char* hwfile)
{
    struct sr_vns_if vns_if;
    FILE* fp = 0;
    char line[1024];
    char buf[SR_NAMELEN];
    char *tmpptr;


    if ( (fp = fopen(hwfile, "r") ) == 0 )
    {
        fprintf(stderr, "Error: could not open cpu hardware info file: %s\n",
                hwfile);
        return -1;
    }

    Debug(" < -- Reading hw info from file %s -- >\n", hwfile);
    while ( fgets( line, 1024, fp) )
    {
        line[1023] = 0; /* -- insurance :) -- */

        /* -- read interface name into buf -- */
        if(! (tmpptr = copy_next_field(fp, line, buf)) )
        {
            fclose(fp);
            fprintf(stderr, "Bad formatting in cpu hardware file\n");
            return 1;
        }
        Debug(" - Name [%s] ", buf);
        strncpy(vns_if.name, buf, SR_NAMELEN);
        /* -- read interface ip into buf -- */
        if(! (tmpptr = copy_next_field(fp, tmpptr, buf)) )
        {
            fclose(fp);
            fprintf(stderr, "Bad formatting in cpu hardware file\n");
            return 1;
        }
        Debug(" IP [%s] ", buf);
        vns_if.ip = asci_to_nboip(buf);

        /* -- read interface mask into buf -- */
        if(! (tmpptr = copy_next_field(fp, tmpptr, buf)) )
        {
            fclose(fp);
            fprintf(stderr, "Bad formatting in cpu hardware file\n");
            return 1;
        }
        Debug(" Mask [%s] ", buf);
        vns_if.mask = asci_to_nboip(buf);

        /* -- read interface hw address into buf -- */
        if(! (tmpptr = copy_next_field(fp, tmpptr, buf)) )
        {
            fclose(fp);
            fprintf(stderr, "Bad formatting in cpu hardware file\n");
            return 1;
        }
        Debug(" MAC [%s]\n", buf);
        asci_to_ether(buf, vns_if.addr);


#ifdef _CPUMODE_
		// open raw socket
		int s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
		if(s < 0){
			perror("socket failed");
		}
		struct ifreq ifr;
		bzero(&ifr, sizeof(struct ifreq));
		strncpy(ifr.ifr_ifrn.ifrn_name, vns_if.name, IFNAMSIZ);
		if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
		        perror("ioctl SIOCGIFINDEX");
		        exit(1);
		}

		struct sockaddr_ll saddr;
		bzero(&saddr, sizeof(struct sockaddr_ll));
		saddr.sll_family = AF_PACKET;
		saddr.sll_protocol = htons(ETH_P_ALL);
		saddr.sll_ifindex = ifr.ifr_ifru.ifru_ivalue;

		if (bind(s, (struct sockaddr*)(&saddr), sizeof(saddr)) < 0) {
	        perror("bind error");
		    exit(1);
		}
		int flags;		if((flags = fcntl(s, F_GETFL, 0)) < 0){		    perror("F_GETFL error");
			exit(1);
		}		flags |= O_NONBLOCK;		if(fcntl(s, F_SETFL, flags) < 0){		    perror("F_ SETFL error");
		    exit(1);
		}
		vns_if.socket = s; // save socket ID
#endif /* _CPUMODE_ */
        
        sr_integ_add_interface(sr, &vns_if);

    } /* -- while ( fgets ( .. ) ) -- */
    Debug(" < --                         -- >\n");

    fclose(fp);
    return 0;

} /* -- sr_cpu_init_hardware -- */

/*-----------------------------------------------------------------------------
 * Method: sr_cpu_input(..)
 * Scope: Local
 *
 *---------------------------------------------------------------------------*/

int sr_cpu_input(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);


#ifdef _CPUMODE_

	int i;
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
	unsigned const int len = 8192;
	int rec_len;
	uint8_t buf[len];

	// this would be better off in multiple threads, but this function is a bad place to do that
	i = 0;
	while(1){
		// A read lock (subsystem->if_lock) should be held here, but in the interest of speed it is not.
		// This is OK, as long as we don't ever modify ifaces array or socket value (which we don't)
		rec_len = recvfrom(subsystem->ifaces[i].socket, buf, len, 0, NULL, 0);
		if ( rec_len > 0 ) break;		
					
		i++;
		i %= subsystem->num_ifaces;
		usleep(10);
	}

	pthread_rwlock_rdlock(&subsystem->if_lock);
    sr_integ_input(sr,
            buf,   							/* lent */
            rec_len,
            subsystem->ifaces[i].name ); 	/* lent */
    pthread_rwlock_unlock(&subsystem->if_lock);
     
    /*
     * Note: To log incoming packets, use sr_log_packet from sr_dumper.[c,h]
     */

    /* RETURN 1 on success, 0 on failure.
     * Note: With a 0 result, the router will shut-down
     */
    return 1;

#else 

	fprintf(stderr, "router in VNS mode!");
	return 0;

#endif /* _CPUMODE_ */


} /* -- sr_cpu_input -- */

/*-----------------------------------------------------------------------------
 * Method: sr_cpu_output(..)
 * Scope: Global
 *
 *---------------------------------------------------------------------------*/

int sr_cpu_output(struct sr_instance* sr /* borrowed */,
                       uint8_t* buf /* borrowed */ ,
                       unsigned int len,
                       const char* iface /* borrowed */)
{
    /* REQUIRES */
    assert(sr);
    assert(buf);
    assert(iface);

#ifdef _CPUMODE_

	int i, retVal;
	struct sr_router* subsystem = (struct sr_router*)sr_get_subsystem(sr);
		
	pthread_rwlock_rdlock(&subsystem->if_lock);
	for(i = 0; i < subsystem->num_ifaces; i++){
		if(!strcmp(iface, subsystem->ifaces[i].name)){
			retVal = sendto(subsystem->ifaces[i].socket, buf, len, 0, NULL, 0);			
			pthread_rwlock_unlock(&subsystem->if_lock);
			return retVal;
		}
	}

	pthread_rwlock_unlock(&subsystem->if_lock);

#endif /* _CPUMODE_ */

    /* Return the length of the packet on success, -1 on failure */
    return -1;
} /* -- sr_cpu_output -- */


/*-----------------------------------------------------------------------------
 * Method: copy_next_field(..)
 * Scope: Local
 *
 *---------------------------------------------------------------------------*/

static
char* copy_next_field(FILE* fp, char* line, char* buf)
{
    char* tmpptr = buf;
    while ( *line  && isspace((int)*line)) /* -- XXX: potential overrun here */
    { line++; }
    if(! *line )
    { return 0; }
    while ( *line && ! isspace((int)*line) && ((tmpptr - buf) < SR_NAMELEN))
    { *tmpptr++ = *line++; }
    *tmpptr = 0;
    return line;
} /* -- copy_next_field -- */

/*-----------------------------------------------------------------------------
 * Method: asci_to_nboip(..)
 * Scope: Local
 *
 *---------------------------------------------------------------------------*/

static uint32_t asci_to_nboip(const char* ip)
{
    struct in_addr addr;

    if ( inet_pton(AF_INET, ip, &addr) <= 0 )
    { return 0; } /* -- 0.0.0.0 unsupported so its ok .. yeah .. really -- */

    return addr.s_addr;
} /* -- asci_to_nboip -- */

/*-----------------------------------------------------------------------------
 * Method: asci_to_ether(..)
 * Scope: Local
 *
 * Look away .. please ... just look away
 *
 *---------------------------------------------------------------------------*/

static void asci_to_ether(const char* addr, uint8_t mac[6])
{
    uint32_t tmpint;
    const char* buf = addr;
    int i = 0;
    for( i = 0; i < 6; ++i )
    {
        if (i)
        {
            while (*buf && *buf != ':')
            { buf++; }
            buf++;
        }
        sscanf(buf, "%x", &tmpint);
        mac[i] = tmpint & 0x000000ff;
    }
} /* -- asci_to_ether -- */
