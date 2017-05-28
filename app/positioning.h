#ifndef POSITIONING_H
#define POSITIONING_H
#include "trickle.h"


/** \brief Initialize positioning module. Will also call `trickle_init`.
*
*/
void
positioning_init(void);


/** \brief Generate key based on the index of the trickle instance in the 
*          `instances` array. Returns number of bytes written do `dest`.
*
* /param[in]  instance                Pointer to trickle instance.
* /param[in]  dest                    Pointer to where the key wil be written.    (?)
*/
uint8_t
positioning_get_key(uint8_t *instance, uint8_t *dest);


/** \brief (?)
*
* /param[in]  instance              Pointer to trickle instance.
*/
slice_t
positioning_get_val(uint8_t *instance);


/** \brief Returns trickle instance based on key. Will register addresses 
*          contained if they are not already registered in the internal 
*          `addresses` array.
*
* \param[in]  key                   Key generated from trickle instances. 
*/
struct trickle_t*
positioning_get_instance(slice_t key);


/** \brief Registers read RSSI value to trickle instance. 
*
* \param[in] rssi                   RSSI value read incoming packet.
* \param[in] other_dev_addr         Address of packet sender. 
*/
void
positioning_register_rssi(uint8_t rssi, uint8_t *other_dev_addr);


/** \brief Returns 1 if `addr` is registered as a device which sends 
*          positioning packets.
*
* \param[in] addr                   Address of packet recieved. 
*/
uint8_t
is_positioning_node(uint8_t *addr);


/** \brief Prints out stored trickle data. Used for debugging.
*
*/
void
positioning_print(void);
#endif
