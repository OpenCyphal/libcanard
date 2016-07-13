/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 */

#ifndef CANARD_AVR_H
#define CANARD_AVR_H

#include <canard.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * \ingroup   communication
 * \defgroup  canard_avr_interface Libcanard CAN Interface for AVR microcontrollers
 * \brief     Interface for Libcanard CAN interaction with AVR microcontrollers
 *
 * \author    Matthias Renner <rennerm@ethz.ch>
 * \author    ETH Zuerich Robotics Systems Lab (http://http://www.rsl.ethz.ch/)
 *
 * \version   0.1
 */


/**
 * @ingroup canard_avr_interface
 * @brief Initialize CAN interface on AVR microcontroller.
 * @warning Enables interrupts!
 *
 * @param [in] id  CAN ID for hardware filter (to be implemented)
 *
 * @retval     0   Successfully initialized.
 */
int canardAVRInit(uint32_t id);

/**
 * @ingroup canard_avr_interface
 * @brief Deinitialize CAN interface on AVR microcontroller.
 * @warning Not implemented
 *
 * @retval 0
 */
int canardAVRClose(void);

/**
 * @ingroup canard_avr_interface
 * @brief Transmits a CanardCANFrame to the CAN device.
 *
 * @param [in] frame  Canard CAN frame which contains the data to send
 *
 * @retval     0      No CAN send buffer free
 * @retval     -1     Error, data could not be sent
 * @retval     1      Data sent successful
 */
int canardAVRTransmit(const CanardCANFrame* frame);

/**
 * @ingroup canard_avr_interface
 * @brief Receives a CanardCANFrame from the CAN device.
 *
 * @param [out] out_frame  Canard CAN frame which contains data received
 *
 * @retval      0          No new CAN data to be read
 * @retval      -1         Error, data could not be read
 * @retval      1          Data read successful
 */
int canardAVRReceive(CanardCANFrame* out_frame);

#ifdef __cplusplus
}
#endif

#endif
