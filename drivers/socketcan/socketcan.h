/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 */

#ifndef SOCKETCAN_H
#define SOCKETCAN_H

#include <canard.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    int fd;
} SocketCANInstance;

/**
 * Initializes the SocketCAN instance.
 */
int socketcanInit(SocketCANInstance* out_ins, const char* can_iface_name);

/**
 * Deinitializes the SocketCAN instance.
 */
int socketcanClose(SocketCANInstance* ins);

/**
 * Transmits a CanardCANFrame to the CAN socket.
 * Use negative timeout to block infinitely.
 */
int socketcanTransmit(SocketCANInstance* ins, const CanardCANFrame* frame, int timeout_msec);

/**
 * Receives a CanardCANFrame from the CAN socket.
 * Use negative timeout to block infinitely.
 */
int socketcanReceive(SocketCANInstance* ins, CanardCANFrame* out_frame, int timeout_msec);

/**
 * Returns the file descriptor of the CAN socket.
 */
int socketcanGetSocketFileDescriptor(const SocketCANInstance* ins);

#ifdef __cplusplus
}
#endif

#endif
