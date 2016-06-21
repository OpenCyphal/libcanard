/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 */

#ifndef NUTTX_H
#define NUTTX_H

#include <canard.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    int fd;
} NuttXInstance;

/**
 * Initializes the NuttX instance.
 */
int nuttxInit(NuttXInstance* out_ins, const char* can_iface_name);

/**
 * Deinitializes the NuttX instance.
 */
int nuttxClose(NuttXInstance* ins);

/**
 * Transmits a CanardCANFrame to the CAN device.
 */
int nuttxTransmit(NuttXInstance* ins, const CanardCANFrame* frame, int timeout_msec);

/**
 * Receives a CanardCANFrame from the CAN device.
 */
int nuttxReceive(NuttXInstance* ins, CanardCANFrame* out_frame, int timeout_msec);

/**
 * Returns the file descriptor of the CAN device.
 */
int nuttxGetDeviceFileDescriptor(const NuttXInstance* ins);

#ifdef __cplusplus
}
#endif

#endif
