// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal.
// Author: Pavel Kirienko <pavel@opencyphal.org>
//
// Minimal heartbeat monitor demo using GNU/Linux SocketCAN.
// Subscribes to Cyphal v1 heartbeats (subject 7509) and DroneCAN NodeStatus (DTID 341) simultaneously,
// and prints a refreshing table of all detected nodes with their uptime.
//
// Usage: heartbeat_monitor [can_interface]
//        Default interface: can0
//
// Quick test with virtual CAN:
//   sudo modprobe vcan && sudo ip link add vcan0 type vcan && sudo ip link set up vcan0
//   ./heartbeat_monitor vcan0

#define _DEFAULT_SOURCE // For clock_gettime, struct timespec, etc.
#include <canard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define CYPHAL_HEARTBEAT_SUBJECT_ID    7509U
#define CYPHAL_HEARTBEAT_EXTENT        7U // 6 bytes serialized + margin
#define DRONECAN_NODE_STATUS_DTID      341U
#define DRONECAN_NODE_STATUS_SIGNATURE 0x0F0868D0C1A7C6F1ULL
#define DRONECAN_NODE_STATUS_EXTENT    7U        // 7 bytes serialized
#define NODE_OFFLINE_TIMEOUT_US        5000000LL // 5 seconds

// ----------------------------------------  Node table  ----------------------------------------

typedef struct
{
    uint32_t      uptime;
    int64_t       last_seen_us;
    uint_least8_t health;
    uint_least8_t mode;
    bool          seen;
} node_entry_t;

enum
{
    PROTO_CYPHAL   = 0,
    PROTO_DRONECAN = 1,
    PROTO_COUNT    = 2,
};

static node_entry_t g_nodes[PROTO_COUNT][CANARD_NODE_ID_MAX + 1U];

static const char* const g_health_str[] = { "NOMINAL", "ADVISORY", "CAUTION", "WARNING" };
static const char* const g_mode_str[]   = { "OPERATIONAL", "INITIALIZATION", "MAINTENANCE", "SOFTWARE_UPDATE" };
static const char* const g_proto_str[]  = { "Cyphal", "DroneCAN" };

// ----------------------------------------  Platform  ----------------------------------------

static int64_t get_monotonic_us(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

static void mem_free(const canard_mem_t mem, const size_t size, void* const ptr)
{
    (void)mem;
    (void)size;
    free(ptr);
}
static void* mem_alloc(const canard_mem_t mem, const size_t size)
{
    (void)mem;
    return malloc(size);
}
static const canard_mem_vtable_t g_mem_vtable = { .free = mem_free, .alloc = mem_alloc };

// ----------------------------------------  Canard vtable  ----------------------------------------

static canard_us_t vtable_now(const canard_t* const self)
{
    (void)self;
    return get_monotonic_us();
}
static bool vtable_tx(canard_t* const      self,
                      void* const          user_context,
                      const canard_us_t    deadline,
                      const uint_least8_t  iface_index,
                      const bool           fd,
                      const uint32_t       extended_can_id,
                      const canard_bytes_t can_data)
{
    (void)self;
    (void)user_context;
    (void)deadline;
    (void)iface_index;
    (void)fd;
    (void)extended_can_id;
    (void)can_data;
    return false; // Receive-only demo.
}
static const canard_vtable_t g_canard_vtable = { .now = vtable_now, .tx = vtable_tx, .filter = NULL };

// ----------------------------------------  Subscription callbacks  ----------------------------------------

static void update_node(const int proto, const uint_least8_t node_id, const canard_payload_t payload)
{
    const size_t min_size = (proto == PROTO_CYPHAL) ? 6U : 7U;
    if ((payload.view.size < min_size) || (node_id > CANARD_NODE_ID_MAX)) {
        goto cleanup;
    }
    const uint_least8_t* const d = (const uint_least8_t*)payload.view.data;
    node_entry_t* const        n = &g_nodes[proto][node_id];
    n->uptime       = (uint32_t)d[0] | ((uint32_t)d[1] << 8U) | ((uint32_t)d[2] << 16U) | ((uint32_t)d[3] << 24U);
    n->health       = d[4] & 0x03U;
    n->mode         = (d[4] >> 2U) & 0x07U;
    n->last_seen_us = get_monotonic_us();
    n->seen         = true;
cleanup:
    if (payload.origin.data != NULL) {
        free(payload.origin.data);
    }
}

static void on_cyphal_heartbeat(canard_subscription_t* const self,
                                const canard_us_t            timestamp,
                                const canard_prio_t          priority,
                                const uint_least8_t          source_node_id,
                                const uint_least8_t          transfer_id,
                                const canard_payload_t       payload)
{
    (void)self;
    (void)timestamp;
    (void)priority;
    (void)transfer_id;
    update_node(PROTO_CYPHAL, source_node_id, payload);
}

static void on_dronecan_heartbeat(canard_subscription_t* const self,
                                  const canard_us_t            timestamp,
                                  const canard_prio_t          priority,
                                  const uint_least8_t          source_node_id,
                                  const uint_least8_t          transfer_id,
                                  const canard_payload_t       payload)
{
    (void)self;
    (void)timestamp;
    (void)priority;
    (void)transfer_id;
    update_node(PROTO_DRONECAN, source_node_id, payload);
}

static const canard_subscription_vtable_t g_sub_vtable_cyphal   = { .on_message = on_cyphal_heartbeat };
static const canard_subscription_vtable_t g_sub_vtable_dronecan = { .on_message = on_dronecan_heartbeat };

// ----------------------------------------  Display  ----------------------------------------

static void print_table(const char* const iface_name)
{
    const int64_t now = get_monotonic_us();
    // Move cursor home and clear screen.
    (void)fputs("\033[H\033[2J", stdout);
    (void)printf("Heartbeat Monitor \xe2\x80\x94 %s\n\n", iface_name);
    (void)puts(" Protocol  \xe2\x94\x82 Node \xe2\x94\x82   Uptime \xe2\x94\x82 Health    \xe2\x94\x82 Mode");
    (void)puts(
      "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2"
      "\x94\x80"
      "\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
      "\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
      "\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2"
      "\x94\x80\xe2\x94\x80\xe2\x94\x80"
      "\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2"
      "\x94\x80\xe2\x94\x80"
      "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80");
    size_t count = 0;
    for (int proto = 0; proto < PROTO_COUNT; proto++) {
        for (size_t nid = 0; nid <= CANARD_NODE_ID_MAX; nid++) {
            const node_entry_t* const n = &g_nodes[proto][nid];
            if (n->seen && ((now - n->last_seen_us) < NODE_OFFLINE_TIMEOUT_US)) {
                const char* const health = (n->health < 4U) ? g_health_str[n->health] : "?";
                const char* const mode   = (n->mode < 4U) ? g_mode_str[n->mode] : "?";
                (void)printf(" %-9s \xe2\x94\x82 %4zu \xe2\x94\x82 %6us \xe2\x94\x82 %-9s \xe2\x94\x82 %s\n",
                             g_proto_str[proto],
                             nid,
                             n->uptime,
                             health,
                             mode);
                count++;
            }
        }
    }
    if (count == 0) {
        (void)puts(" (no nodes detected)");
    }
    (void)printf("\n%zu node%s online\n", count, (count == 1) ? "" : "s");
    (void)fflush(stdout);
}

// ----------------------------------------  SocketCAN  ----------------------------------------

static int open_can_socket(const char* const iface_name)
{
    const int fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    struct ifreq ifr;
    (void)memset(&ifr, 0, sizeof(ifr));
    (void)strncpy(ifr.ifr_name, iface_name, sizeof(ifr.ifr_name) - 1U);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        (void)close(fd);
        return -1;
    }
    struct sockaddr_can addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        (void)close(fd);
        return -1;
    }
    return fd;
}

// ----------------------------------------  Main  ----------------------------------------

static volatile sig_atomic_t g_running = 1;
static void                  on_sigint(const int sig)
{
    (void)sig;
    g_running = 0;
}

int main(const int argc, const char* const argv[])
{
    const char* const iface_name = (argc > 1) ? argv[1] : "can0";

    // Open SocketCAN.
    const int sock = open_can_socket(iface_name);
    if (sock < 0) {
        return 1;
    }

    // Initialize canard.
    const canard_mem_t mem = { .vtable = &g_mem_vtable, .context = NULL };
    canard_t           ins;
    if (!canard_new(&ins,
                    &g_canard_vtable,
                    (canard_mem_set_t){
                      .tx_transfer = mem,
                      .tx_frame    = mem,
                      .rx_session  = mem,
                      .rx_payload  = mem,
                      .rx_filters  = mem,
                    },
                    0,  // tx_queue_capacity: receive-only
                    0,  // prng_seed: irrelevant for receive-only
                    0)) // filter_count: no HW filters
    {
        (void)fputs("canard_new failed\n", stderr);
        (void)close(sock);
        return 1;
    }

    // Subscribe to Cyphal heartbeat (subject 7509, 13-bit subject-ID space).
    canard_subscription_t sub_cyphal;
    if (!canard_subscribe_13b(&ins,
                              &sub_cyphal,
                              CYPHAL_HEARTBEAT_SUBJECT_ID,
                              CYPHAL_HEARTBEAT_EXTENT,
                              CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us,
                              &g_sub_vtable_cyphal)) {
        (void)fputs("canard_subscribe_13b failed\n", stderr);
        canard_destroy(&ins);
        (void)close(sock);
        return 1;
    }

    // Subscribe to DroneCAN NodeStatus (DTID 341).
    canard_subscription_t sub_dronecan;
    const uint16_t dronecan_crc_seed = canard_v0_crc_seed_from_data_type_signature(DRONECAN_NODE_STATUS_SIGNATURE);
    if (!canard_v0_subscribe(&ins,
                             &sub_dronecan,
                             DRONECAN_NODE_STATUS_DTID,
                             dronecan_crc_seed,
                             DRONECAN_NODE_STATUS_EXTENT,
                             CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us,
                             &g_sub_vtable_dronecan)) {
        (void)fputs("canard_v0_subscribe failed\n", stderr);
        canard_unsubscribe(&ins, &sub_cyphal);
        canard_destroy(&ins);
        (void)close(sock);
        return 1;
    }

    (void)signal(SIGINT, on_sigint);
    (void)signal(SIGTERM, on_sigint);
    (void)printf("Listening on %s... (Ctrl+C to exit)\n", iface_name);

    int64_t last_display_us = 0;
    while (g_running) {
        struct pollfd pfd = { .fd = sock, .events = POLLIN, .revents = 0 };
        const int     pr  = poll(&pfd, 1, 100); // 100 ms timeout
        if (pr > 0 && (pfd.revents & POLLIN)) {
            struct can_frame frame;
            const ssize_t    nbytes = read(sock, &frame, sizeof(frame));
            if (nbytes == (ssize_t)sizeof(frame)) {
                // Only process extended data frames (both Cyphal and DroneCAN use 29-bit IDs).
                if ((frame.can_id & CAN_EFF_FLAG) && !(frame.can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG))) {
                    const canard_bytes_t data = { .size = frame.can_dlc, .data = frame.data };
                    (void)canard_ingest_frame(&ins, get_monotonic_us(), 0, frame.can_id & CAN_EFF_MASK, data);
                }
            }
        }
        canard_poll(&ins, 0);

        // Refresh the display approximately once per second.
        const int64_t now = get_monotonic_us();
        if ((now - last_display_us) >= 1000000LL) {
            last_display_us = now;
            print_table(iface_name);
        }
    }

    (void)fputs("\nShutting down.\n", stdout);
    canard_unsubscribe(&ins, &sub_dronecan);
    canard_unsubscribe(&ins, &sub_cyphal);
    canard_destroy(&ins);
    (void)close(sock);
    return 0;
}
