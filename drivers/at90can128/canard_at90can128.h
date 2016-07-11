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
 * Initializes the AVR instance.
 */
int canardAVRInit(void);

/**
 * Deinitializes the AVR instance.
 */
int canardAVRClose(void);

/**
 * Transmits a CanardCANFrame to the CAN device.
 */
int canardAVRTransmit(const CanardCANFrame* frame);

/**
 * Receives a CanardCANFrame from the CAN device.
 */
int canardAVRReceive(CanardCANFrame* out_frame);

#ifdef __cplusplus
}
#endif

#endif
