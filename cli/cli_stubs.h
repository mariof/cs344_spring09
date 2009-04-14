/**
 * Filename: cli_stubs.c
 * Author: David Underhill
 */

#ifndef CLI_STUBS_H
#define CLI_STUBS_H


/**
 * Enables or disables an interface on the router.
 * @return 0 if name was enabled
 *         -1 if it does not not exist
 *         1 if already set to enabled
 */
int router_interface_set_enabled( struct sr_instance* sr, const char* name, int enabled ) {
    fprintf( stderr, "not yet implemented: router_interface_set_enabled\n" );
    return -1;
}

/**
 * Returns a pointer to the interface which is assigned the specified IP.
 *
 * @return interface, or NULL if the IP does not belong to any interface
 *         (you'll want to change void* to whatever type you end up using)
 */
void* router_lookup_interface_via_ip( struct sr_instance* sr,
                                      uint32_t ip ) {
    fprintf( stderr, "not yet implemented: router_lookup_interface_via_ip\n" );
    return NULL;
}

/**
 * Returns a pointer to the interface described by the specified name.
 *
 * @return interface, or NULL if the name does not match any interface
 *         (you'll want to change void* to whatever type you end up using)
 */
void* router_lookup_interface_via_name( struct sr_instance* sr,
                                        const char* name ) {
    fprintf( stderr, "not yet implemented: router_lookup_interface_via_name\n" );
    return NULL;
}

/**
 * Returns 1 if the specified interface is up and 0 otherwise.
 */
int router_is_interface_enabled( struct sr_instance* sr, void* intf ) {
    fprintf( stderr, "not yet implemented: router_is_interface_enabled\n" );
    return 0;
}

/**
 * Returns whether OSPF is enabled (0 if disabled, otherwise it is enabled).
 */
int router_is_ospf_enabled( struct sr_instance* sr ) {
    fprintf( stderr, "not yet implemented: router_is_ospf_enabled\n" );
    return 0;
}

/**
 * Sets whether OSPF is enabled.
 */
void router_set_ospf_enabled( struct sr_instance* sr, int enabled ) {
    fprintf( stderr, "not yet implemented: router_set_ospf_enabled\n" );
}

#endif /* CLI_STUBS_H */
