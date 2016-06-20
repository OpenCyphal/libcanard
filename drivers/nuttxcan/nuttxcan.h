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
} NuttXCANInstance;

int nuttxcanInit(NuttXCANInstance* out_ins, const char* can_iface_name);
int nuttxcanClose(NuttXCANInstance* ins);
int nuttxcanTransmit(NuttXCANInstance* ins, const CanardCANFrame* frame, int timeout_msec);
int nuttxcanReceive(NuttXCANInstance* ins, CanardCANFrame* out_frame, int timeout_msec);
int nuttxcanGetDeviceFileDescriptor(const NuttXCANInstance* ins);
