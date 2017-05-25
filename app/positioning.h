#ifndef POSITIONING_H
#define POSITIONING_H
#include "trickle.h"

/* Initialize positioning module. This will call `trickle_init`.
 */
void
positioning_init();



// Interface for Trickle. Look in documentation at trickle.h

/* (See trickle docs)
 */
uint8_t
positioning_get_key(uint8_t *instance, uint8_t *dest);

/* (See trickle docs)
 */
slice_t
positioning_get_val(uint8_t *instance);

/* (See trickle docs)
 * BEWARE: Will register the addresses contained in the key if they are not
 * already registered in the internal `addresses` array.
 */
struct trickle_t*
positioning_get_instance(slice_t key);




// Other functions for application:

/* Registers the RSSI between this device and `other_dev_addr`, via Trickle.
 * I.e. updates the value.
 */
void
positioning_register_rssi(uint8_t rssi, uint8_t *other_dev_addr);

/* Returns 1 iff `addr` is registered as a device which sends positioning packets.
 */
uint8_t
is_positioning_node(uint8_t *addr);

void
positioning_print();
#endif
