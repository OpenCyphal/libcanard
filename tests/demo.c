/*
 * This demo application is distributed under the terms of CC0 (public domain dedication).
 * More info: https://creativecommons.org/publicdomain/zero/1.0/
 */

// This is needed to enable necessary declarations in sys/
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <canard.h>
#include <socketcan.h>      // CAN backend driver for SocketCAN, distributed with Libcanard

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

/*
 * Some useful constants defined by the UAVCAN specification.
 * Data type signature values can be easily obtained with the script get_data_type_signature.py
 */
#define UAVCAN_NODE_ID_ALLOCATION_DATA_TYPE_ID                      1
#define UAVCAN_NODE_ID_ALLOCATION_DATA_TYPE_SIGNATURE               0x0b2a812620a11d40
#define UAVCAN_NODE_ID_ALLOCATION_RANDOM_TIMEOUT_RANGE_USEC         400000UL
#define UAVCAN_NODE_ID_ALLOCATION_REQUEST_DELAY_OFFSET_USEC         600000UL

#define UNIQUE_ID_LENGTH_BYTES                                      16


/*
 * Library instance.
 * In simple applications it makes sense to make it static, but it is not necessary.
 */
static CanardInstance canard;                       ///< The library instance
static uint8_t canard_memory_pool[1024];            ///< Arena for memory allocation, used by the library

/*
 * Variables used for dynamic node ID allocation.
 * RTFM at http://uavcan.org/Specification/6._Application_level_functions/#dynamic-node-id-allocation
 */
static uint64_t send_next_node_id_allocation_request_at;    ///< When the next node ID allocation request should be sent
static uint8_t node_id_allocation_unique_id_offset;         ///< Depends on the stage of the next request


uint64_t getMonotonicTimestampUSec(void)
{
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        abort();
    }
    return ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}


/**
 * Returns a pseudo random float in the range [0, 1].
 */
float getRandomFloat(void)
{
    static bool initialized = false;
    if (!initialized)                   // This is not thread safe, but a race condition here is not harmful.
    {
        initialized = true;
        srand((unsigned)time(NULL));
    }
    // coverity[dont_call]
    return (float)rand() / (float)RAND_MAX;
}


/**
 * This function uses a mock unique ID, this is not allowed in real applications!
 */
void readUniqueID(uint8_t* out_uid)
{
    for (uint8_t i = 0; i < UNIQUE_ID_LENGTH_BYTES; i++)
    {
        out_uid[i] = i;
    }
}


/**
 * This callback is invoked by the library when a new message or request or response is received.
 */
static void onTransferReceived(CanardInstance* ins,
                               CanardRxTransfer* transfer)
{
    /*
     * Dynamic node ID allocation protocol.
     * Taking this branch only if we don't have a node ID, ignoring otherwise.
     */
    if ((canardGetLocalNodeID(ins) == CANARD_BROADCAST_NODE_ID) &&
        (transfer->transfer_type == CanardTransferTypeBroadcast) &&
        (transfer->data_type_id == UAVCAN_NODE_ID_ALLOCATION_DATA_TYPE_ID))
    {
        // Rule C - updating the randomized time interval
        send_next_node_id_allocation_request_at =
            getMonotonicTimestampUSec() + UAVCAN_NODE_ID_ALLOCATION_REQUEST_DELAY_OFFSET_USEC +
            (uint64_t)(getRandomFloat() * UAVCAN_NODE_ID_ALLOCATION_RANDOM_TIMEOUT_RANGE_USEC);

        if (transfer->source_node_id == CANARD_BROADCAST_NODE_ID)
        {
            puts("Allocation request from another allocatee");
            return;
        }

        // Copying the unique ID from the message
        static const unsigned UniqueIDBitOffset = 8;
        uint8_t received_unique_id[UNIQUE_ID_LENGTH_BYTES];
        uint8_t received_unique_id_len = 0;
        for (; received_unique_id_len < (transfer->payload_len - (UniqueIDBitOffset / 8U)); received_unique_id_len++)
        {
            assert(received_unique_id_len < UNIQUE_ID_LENGTH_BYTES);
            const uint8_t bit_offset = (uint8_t)(UniqueIDBitOffset + received_unique_id_len * 8U);
            received_unique_id[received_unique_id_len] = (uint8_t)canardReadRxTransferPayload(transfer, bit_offset, 8);
        }

        // Obtaining the local unique ID
        uint8_t my_unique_id[UNIQUE_ID_LENGTH_BYTES];
        readUniqueID(my_unique_id);

        // Matching the received UID against the local one
        if (memcmp(received_unique_id, my_unique_id, received_unique_id_len) != 0)
        {
            printf("Mismatching allocation response from %d\n", transfer->source_node_id);
            return;         // No match, return
        }

        if (received_unique_id_len < UNIQUE_ID_LENGTH_BYTES)
        {
            // The allocator has confirmed part of unique ID, switching to the next stage and updating the timeout.
            node_id_allocation_unique_id_offset = received_unique_id_len;
            send_next_node_id_allocation_request_at -= UAVCAN_NODE_ID_ALLOCATION_REQUEST_DELAY_OFFSET_USEC;

            printf("Matching allocation response from %d offset %d\n",
                   transfer->source_node_id, node_id_allocation_unique_id_offset);
        }
        else
        {
            // Allocation complete - copying the allocated node ID from the message
            const uint8_t allocated_node_id = (uint8_t)canardReadRxTransferPayload(transfer, 0, 7);
            assert(allocated_node_id <= 127);

            canardSetLocalNodeID(ins, allocated_node_id);
            printf("Node ID %d allocated by %d\n", allocated_node_id, transfer->source_node_id);
        }
    }
}


/**
 * This callback is invoked by the library when it detects beginning of a new transfer on the bus that can be received
 * by the local node.
 * If the callback returns true, the library will receive the transfer.
 * If the callback returns false, the library will ignore the transfer.
 * All transfers that are addressed to other nodes are always ignored.
 */
static bool shouldAcceptTransfer(const CanardInstance* ins,
                                 uint64_t* out_data_type_signature,
                                 uint16_t data_type_id,
                                 CanardTransferType transfer_type,
                                 uint8_t source_node_id)
{
    (void)source_node_id;

    if (canardGetLocalNodeID(ins) == CANARD_BROADCAST_NODE_ID)
    {
        /*
         * If we're in the process of allocation of dynamic node ID, accept only relevant transfers.
         */
        if ((transfer_type == CanardTransferTypeBroadcast) &&
            (data_type_id == UAVCAN_NODE_ID_ALLOCATION_DATA_TYPE_ID))
        {
            *out_data_type_signature = UAVCAN_NODE_ID_ALLOCATION_DATA_TYPE_SIGNATURE;
            return true;
        }
    }
    else
    {
    }

    return false;
}


/**
 * This function is called at 1 Hz rate from the main loop.
 */
void process1HzTasks(void)
{
    // TODO
}


/**
 * Transmits all frames from the TX queue, receives up to one frame.
 */
void processTxRxOnce(SocketCANInstance* socketcan, int timeout_msec)
{
    // Transmitting
    for (const CanardCANFrame* txf = canardPeekTxQueue(&canard); (txf = canardPeekTxQueue(&canard)) != NULL;)
    {
        const int tx_res = socketcanTransmit(socketcan, txf, 0);
        if (tx_res < 0)         // Failure - drop the frame and report
        {
            canardPopTxQueue(&canard);
            (void)fprintf(stderr, "Transmit error %d, frame dropped, errno '%s'\n", tx_res, strerror(errno));
        }
        else if (tx_res > 0)    // Success - just drop the frame
        {
            canardPopTxQueue(&canard);
        }
        else                    // Timeout - just exit and try again later
        {
            break;
        }
    }

    // Receiving
    CanardCANFrame rx_frame;
    const uint64_t timestamp = getMonotonicTimestampUSec();
    const int rx_res = socketcanReceive(socketcan, &rx_frame, timeout_msec);
    if (rx_res < 0)             // Failure - report
    {
        (void)fprintf(stderr, "Receive error %d, errno '%s'\n", rx_res, strerror(errno));
    }
    else if (rx_res > 0)        // Success - process the frame
    {
        canardHandleRxFrame(&canard, &rx_frame, timestamp);
    }
    else
    {
        ;                       // Timeout - nothing to do
    }
}


int main(int argc, char** argv)
{
    if (argc < 2)
    {
        (void)fprintf(stderr,
                      "Usage:\n"
                      "\t%s <can iface name>\n",
                      argv[0]);
        return 1;
    }

    /*
     * Initializing the CAN backend driver; in this example we're using SocketCAN
     */
    SocketCANInstance socketcan;
    const char* const can_iface_name = argv[1];
    int res = socketcanInit(&socketcan, can_iface_name);
    if (res < 0)
    {
        (void)fprintf(stderr, "Failed to open CAN iface '%s'\n", can_iface_name);
        return 1;
    }

    /*
     * Initializing the Libcanard instance.
     */
    canardInit(&canard, canard_memory_pool, sizeof(canard_memory_pool), onTransferReceived, shouldAcceptTransfer);

    /*
     * Performing the dynamic node ID allocation procedure.
     */
    static const uint8_t PreferredNodeID = CANARD_BROADCAST_NODE_ID;    ///< This can be made configurable, obviously

    node_id_allocation_unique_id_offset = 0;

    uint8_t node_id_allocation_transfer_id = 0;

    while (canardGetLocalNodeID(&canard) == CANARD_BROADCAST_NODE_ID)
    {
        puts("Waiting for dynamic node ID allocation...");

        send_next_node_id_allocation_request_at =
            getMonotonicTimestampUSec() + UAVCAN_NODE_ID_ALLOCATION_REQUEST_DELAY_OFFSET_USEC +
            (uint64_t)(getRandomFloat() * UAVCAN_NODE_ID_ALLOCATION_RANDOM_TIMEOUT_RANGE_USEC);

        while ((getMonotonicTimestampUSec() < send_next_node_id_allocation_request_at) &&
               (canardGetLocalNodeID(&canard) == CANARD_BROADCAST_NODE_ID))
        {
            processTxRxOnce(&socketcan, 1);
        }

        if (canardGetLocalNodeID(&canard) != CANARD_BROADCAST_NODE_ID)
        {
            break;
        }

        // Structure of the request is documented in the DSDL definition
        // See http://uavcan.org/Specification/6._Application_level_functions/#dynamic-node-id-allocation
        uint8_t allocation_request[CANARD_CAN_FRAME_MAX_DATA_LEN - 1];
        allocation_request[0] = PreferredNodeID << 1;

        if (node_id_allocation_unique_id_offset == 0)
        {
            allocation_request[0] |= 1;     // First part of unique ID
        }

        uint8_t my_unique_id[UNIQUE_ID_LENGTH_BYTES];
        readUniqueID(my_unique_id);

        static const uint8_t MaxLenOfUniqueIDInRequest = 6;
        uint8_t uid_size = (uint8_t)(UNIQUE_ID_LENGTH_BYTES - node_id_allocation_unique_id_offset);
        if (uid_size > MaxLenOfUniqueIDInRequest)
        {
            uid_size = MaxLenOfUniqueIDInRequest;
        }

        // Paranoia time
        assert(node_id_allocation_unique_id_offset < UNIQUE_ID_LENGTH_BYTES);
        assert(uid_size <= MaxLenOfUniqueIDInRequest);
        assert(uid_size > 0);
        assert((uid_size + node_id_allocation_unique_id_offset) <= UNIQUE_ID_LENGTH_BYTES);

        memmove(&allocation_request[1], &my_unique_id[node_id_allocation_unique_id_offset], uid_size);

        // Broadcasting the request
        const int bcast_res = canardBroadcast(&canard,
                                              UAVCAN_NODE_ID_ALLOCATION_DATA_TYPE_SIGNATURE,
                                              UAVCAN_NODE_ID_ALLOCATION_DATA_TYPE_ID,
                                              &node_id_allocation_transfer_id,
                                              CANARD_TRANSFER_PRIORITY_LOW,
                                              &allocation_request[0],
                                              (uint16_t) (uid_size + 1));
        if (bcast_res < 0)
        {
            (void)fprintf(stderr, "Could not broadcast dynamic node ID allocation request; error %d\n", bcast_res);
        }
    }

    printf("Dynamic node ID allocation complete [%d]\n", canardGetLocalNodeID(&canard));

    /*
     * Running the main loop.
     */
    uint64_t next_1hz_service_at = getMonotonicTimestampUSec();

    for (;;)
    {
        processTxRxOnce(&socketcan, 10);

        const uint64_t ts = getMonotonicTimestampUSec();

        if (ts >= next_1hz_service_at)
        {
            next_1hz_service_at += 1000000;
            process1HzTasks();
        }
    }

    return 0;
}
