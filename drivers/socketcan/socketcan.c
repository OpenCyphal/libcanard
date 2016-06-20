/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 */

#include "socketcan.h"
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/**
 * Initializes the SocketCAN instance.
 */
int socketcanInit(SocketCANInstance* out_ins, const char* can_iface_name)
{
    const size_t iface_name_size = strlen(can_iface_name) + 1;
    if (iface_name_size > IFNAMSIZ)
    {
        goto fail0;
    }

    const int fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (fd < 0)
    {
        goto fail0;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    memcpy(ifr.ifr_name, can_iface_name, iface_name_size);

    const int ioctl_result = ioctl(fd, SIOCGIFINDEX, &ifr);
    if (ioctl_result < 0)
    {
        goto fail1;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    const int bind_result = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (bind_result < 0)
    {
        goto fail1;
    }

    out_ins->fd = fd;
    return 1;

fail1:
    close(fd);
fail0:
    return -1;
}

/**
 * Deinitializes the SocketCAN instance.
 */
int socketcanClose(SocketCANInstance* ins)
{
    const int close_result = close(ins->fd);
    if (close_result < 0)
    {
        return -1;
    }

    return 1;
}

/**
 * Transmits a CanardCANFrame to the CAN socket.
 */
int socketcanTransmit(SocketCANInstance* ins, const CanardCANFrame* frame, int timeout_msec)
{
    struct pollfd fds;
    memset(&fds, 0, sizeof(fds));
    fds.fd = ins->fd;
    fds.events |= POLLOUT;

    const int poll_result = poll(&fds, 1, timeout_msec);
    if (poll_result <= 0)
    {
        return -1;
    }
    if ((fds.revents & POLLOUT) == 0)
    {
        return -1;
    }

    struct can_frame transmit_frame;
    memset(&transmit_frame, 0, sizeof(transmit_frame));
    transmit_frame.can_id = frame->id | CAN_EFF_FLAG;
    transmit_frame.can_dlc = frame->data_len;
    memcpy(transmit_frame.data, frame->data, frame->data_len);

    const ssize_t nbytes = write(ins->fd, &transmit_frame, sizeof(transmit_frame));
    if (nbytes < 0 || (size_t)nbytes != sizeof(transmit_frame))
    {
        return -1;
    }

    return 1;
}

/**
 * Receives a CanardCANFrame from the CAN socket.
 */
int socketcanReceive(SocketCANInstance* ins, CanardCANFrame* out_frame, int timeout_msec)
{
    struct pollfd fds;
    memset(&fds, 0, sizeof(fds));
    fds.fd = ins->fd;
    fds.events |= POLLIN;

    const int poll_result = poll(&fds, 1, timeout_msec);
    if (poll_result <= 0)
    {
        return -1;
    }
    if ((fds.revents & POLLIN) == 0)
    {
        return -1;
    }

    struct can_frame receive_frame;
    const ssize_t nbytes = read(ins->fd, &receive_frame, sizeof(receive_frame));
    if (nbytes < 0 || (size_t)nbytes != sizeof(receive_frame))
    {
        return -1;
    }

    out_frame->id = receive_frame.can_id;
    out_frame->data_len = receive_frame.can_dlc;
    memcpy(out_frame->data, &receive_frame.data, receive_frame.can_dlc);

    return 1;
}

/**
 * Returns the file descriptor of the CAN socket.
 */
int socketcanGetSocketFileDescriptor(const SocketCANInstance* ins)
{
    return ins->fd;
}
