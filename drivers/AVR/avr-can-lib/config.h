#ifndef	CONFIG_H
#define	CONFIG_H

#define CAN_CONFIG_LOADED

// -----------------------------------------------------------------------------
/* Global settings for building the can-lib and application program.
 *
 * The following two #defines must be set identically for the can-lib and
 * your application program. They control the underlying CAN struct. If the
 * settings disagree, the underlying CAN struct will be broken, with
 * unpredictable results.
 * If can.h detects that any of the #defines is not defined, it will set them
 * to the default values shown here, so it is in your own interest to have a
 * consistent setting. Ommiting the #defines in both can-lib and application
 * program will apply the defaults in a consistent way too.
 *
 * Select if you want to use 29 bit identifiers.
 */
#define	SUPPORT_EXTENDED_CANID	1

/* Select if you want to use timestamps.
 * Timestamps are sourced from a register internal to the AT90CAN.
 * Selecting them on any other controller will have no effect, they will
 * be 0 all the time.
 */
#define	SUPPORT_TIMESTAMPS		1


// -----------------------------------------------------------------------------
/* Global settings for building the can-lib.
 *
 * Select ONE CAN controller for which you are building the can-lib. 
 */
#define	SUPPORT_MCP2515			0
#define	SUPPORT_AT90CAN			1
#define	SUPPORT_SJA1000			0

// -----------------------------------------------------------------------------
// Setting for AT90CAN

// Number of CAN messages which are buffered in RAM additinally to the MObs
#define CAN_RX_BUFFER_SIZE		16
#define CAN_TX_BUFFER_SIZE		8

// only available if CAN_TX_BUFFER_SIZE > 0
#define CAN_FORCE_TX_ORDER		1

#endif	// CONFIG_H
