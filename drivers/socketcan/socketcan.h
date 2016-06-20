/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 */

#include "canard.h"

typedef struct
{
    int fd;
} SocketCANInstance;

int socketcanInit(SocketCANInstance* out_ins, const char* can_iface_name);
int socketcanClose(SocketCANInstance* ins);
int socketcanTransmit(SocketCANInstance* ins, const CanardCANFrame* frame, int timeout_msec);
int socketcanReceive(SocketCANInstance* ins, CanardCANFrame* out_frame, int timeout_msec);
int socketcanGetSocketFileDescriptor(const SocketCANInstance* ins);
