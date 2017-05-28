#ifndef TRICKLE_TRICKLE_H
#define TRICKLE_TRICKLE_H
/* Before using this module, ticker and radio should be initialized.
 */

#include <stdint.h>
#include "tx.h"
#include "slice.h"

#define PROTOCOL_ID 0x50607080

struct trickle_t;
#define TRICKLE_T_SIZE 16


#define TICKER_PER_TRICKLE 2 // Instances of ticker per instance of trickle


typedef uint32_t trickle_version_t;
typedef uint16_t trickle_app_id_t;

/* The following functions should be implemented by application and provided in
 * `trickle_config`.
 */


/** \brief Should write the key associated with given `instance`, to `dest`.
*          Returns number of bytes written.
*               
*/
typedef uint8_t             (*trickle_get_key_fp_t)     (uint8_t *instance, uint8_t *dest);


/** \brief Should return the value (ptr, len) associated with an instance.
*
*/
typedef slice_t             (*trickle_get_val_fp_t)     (uint8_t *instance);


/** \brief If key is not found, this function should initialize a trickle struct.
*
*/
typedef struct trickle_t *  (*trickle_get_instance_fp_t)(slice_t key);


/** \brief Trickle configuration struct. Stores information about 
*          the trickle application. 
*
*/
typedef struct {
    // Trickle config
    uint32_t interval_min_us;
    uint32_t interval_max_us;
    uint32_t c_threshold;

    // Ticker
    uint32_t first_ticker_id;

    // Functionality provided by the application
    trickle_get_key_fp_t        get_key_fp;
    trickle_get_val_fp_t        get_val_fp;
    trickle_get_instance_fp_t   get_instance_fp;

    // Other
    uint32_t max_tx_time_us;
} trickle_config_t;


/** \brief trickle_config must be defined by the application
*
*/
extern trickle_config_t trickle_config;


/** \brief Initialization of trickle module. Initializes an array of trickle
*          instances. 
*
* \param[in]  instances             Pointer to array storing trickle instances.    (?)
* \param[in]  n                     Number of trickle instances. 
*/
void
trickle_init(struct trickle_t *instances, uint32_t n);


/** \brief Handles incoming trickle packets. 
*
* \param[in]  packet_ptr            Pointer to packet. 
* \param[in]  packet_len            Length of packet. 
*/
void
trickle_pdu_handle(uint8_t *packet_ptr, uint8_t packet_len);


/** \brief Writes new data to an instance. Increments the version number. 
*
* \param[in]  instance              Pointer to trickle instance.
* \param[in]  key                   Key to where data will be stored.  (?)
* \param[in]  val                   Value of new data.
* \param[in]  user_id               Priority level context for ticker timer. 
*/
void
trickle_value_write(struct trickle_t *instance, slice_t key, slice_t val, uint8_t user_id);


// Serializing (should probably be private)

// Documentation for desse og? (?)
uint32_t
read_uint32(uint8_t *src);
void
write_uint32(uint8_t *dest, uint32_t src);
uint16_t
read_uint16(uint8_t *src);
void
write_uint16(uint8_t *dest, uint16_t src);


/** \brief Toggles a line. Used for debugging. 
*
* \param[in]  line                  Pin number that will be toggled. 
*/
void
toggle_line(uint32_t line);

#endif
