#include "routingTable.h"

void insert_rtable_node(rtableNode **head, uint32_t ip, uint8_t netmask, const char* output_if)
{
    //acquire lock
    //Check if the list is empty
    //scan the list until you hit the right netmask
    //scan for ip in the netmask range
    //malloc new rtableNode, add to the list
    //release lock
}

void del_ip(rtableNode **head, uint32_t ip, uint8_t netmask)
{
    //acquire lock
    //Check if the list is empty
    //scan the list until you hit the right netmask
    //scan for ip in the netmask range
    //delete node
    //release lock
}

/* Return value: 
 * pointer to the interface name if lp match found
 * NULL pointer otherwise
 *
 * Note: the function allocates memory to store the return value
 * The caller must deallocate it after the function returns
 */

char *lp_match(rtableNode **head, uint32_t ip)
{
    //acquire lock
    //do LP matching
    //malloc 32 bytes for storing interface
    //release lock
    //return pointer to the interface buffer
    return NULL;
}

