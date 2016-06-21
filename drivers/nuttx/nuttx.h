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
} CanardNuttXInstance;

/**
 * Initializes the NuttX instance.
 */
int canardNuttXInit(CanardNuttXInstance* out_ins, const char* can_iface_name);

/**
 * Deinitializes the NuttX instance.
 */
int canardNuttXClose(CanardNuttXInstance* ins);

/**
 * Transmits a CanardCANFrame to the CAN device.
 */
int canardNuttXTransmit(CanardNuttXInstance* ins, const CanardCANFrame* frame, int timeout_msec);

/**
 * Receives a CanardCANFrame from the CAN device.
 */
int canardNuttXReceive(CanardNuttXInstance* ins, CanardCANFrame* out_frame, int timeout_msec);

/**
 * Returns the file descriptor of the CAN device.
 */
int canardNuttXGetDeviceFileDescriptor(const CanardNuttXInstance* ins);

#ifdef __cplusplus
}
#endif

#endif
