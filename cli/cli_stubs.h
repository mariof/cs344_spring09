/**
 * Filename: cli_stubs.c
 * Author: David Underhill
 */

#ifndef CLI_STUBS_H
#define CLI_STUBS_H


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
