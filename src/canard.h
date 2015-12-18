/*
 * Copyright (C) 2015 Michael Sierra <sierramichael.a@gmail.com>
 */

#ifndef CANARD_H
#define CANARD_H

#ifdef __cplusplus
extern "C" {
#endif

//#include <std.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/** The size of a memory block in bytes. */
#define CANARD_MEM_BLOCK_SIZE 32
#define CANARD_AVAILABLE_BLOCKS 32

#define TRANSFER_TIMEOUT_USEC 2000000

#define CANARD_CAN_FRAME_MAX_DATA_LEN 8
#define TRANSFER_ID_BIT_LEN 5

#define CANARD_BROADCAST_NODE_ID    0
#define CANARD_MIN_NODE_ID          1
#define CANARD_MAX_NODE_ID          127

#define CANARD_EXT_ID_MASK       0x1FFFFFFFU
#define CANARD_CAN_FRAME_EFF    (1U << 31)  //extended frame format
#define CANARD_CAN_FRAME_RTR    (1U << 30)  //remote transmission
#define CANARD_CAN_FRAME_ERR    (1U << 29)  //error frame

#define CANARD_SOURCE_ID_FROM_ID(x) (x) & (0X7F)
#define CANARD_SERVICE_NOT_MSG_FROM_ID(x)   (((x) >> 7)  & 0X1)
#define CANARD_REQUEST_NOT_RESPONSE_FROM_ID(x) (((x) >> 15) & 0X1)
#define CANARD_DEST_ID_FROM_ID(x)   (((x) >> 8)  & 0X7F)
#define CANARD_PRIORITY_FROM_ID(x)  (((x) >> 24) & 0X1F)
#define CANARD_MSG_TYPE_FROM_ID(x)  (((x) >> 8)  & 0XFFFF)
#define CANARD_SRV_TYPE_FROM_ID(x)  (((x) >> 16)  & 0XFF)

#define TRANSFER_ID_FROM_TAIL_BYTE(x) ((x) & 0X1F)

#define IS_START_OF_TRANSFER(x) (((x) >> 7) & 0X1)
#define IS_END_OF_TRANSFER(x) (((x) >> 6) & 0X1)
#define TOGGLE_BIT(x) (((x) >> 5) & 0X1)

#define CANARD_RX_PAYLOAD_HEAD_SIZE (CANARD_MEM_BLOCK_SIZE - sizeof(CanardRxState))
#define CANARD_BUFFER_BLOCK_DATA_SIZE (CANARD_MEM_BLOCK_SIZE - sizeof(CanardBufferBlock))

typedef struct
{
  uint32_t id;
  uint8_t data[CANARD_CAN_FRAME_MAX_DATA_LEN];
  uint8_t data_len;
} CanardCANFrame;

typedef enum
{
  CanardTransferTypeResponse  = 0,
  CanardTransferTypeRequest   = 1,
  CanardTransferTypeBroadcast = 2
} CanardTransferType;

typedef enum
{
  CanardResponse,
  CanardRequest
} CanardRequestResponse;

typedef struct CanardInstance CanardInstance;
typedef struct CanardRxTransfer CanardRxTransfer;
typedef struct CanardRxState CanardRxState;
typedef struct CanardTxQueueItem CanardTxQueueItem;

typedef bool (*canardShouldAcceptTransferPtr)(const CanardInstance* ins, uint16_t data_type_id, CanardTransferType transfer_type, uint8_t source_node_id);
typedef void (*canardOnTransferReception)(CanardInstance* ins, CanardRxTransfer* transfer);

/** A memory block used in the memory block allocator. */
typedef union CanardPoolAllocatorBlock_u
{
    char bytes[CANARD_MEM_BLOCK_SIZE];
    union CanardPoolAllocatorBlock_u* next;
} CanardPoolAllocatorBlock;

typedef struct
{
    CanardPoolAllocatorBlock* free_list;
} CanardPoolAllocator;

/** buffer block for rx data. */
typedef struct CanardBufferBlock 
{
  struct CanardBufferBlock* next;
  uint8_t data[];
} CanardBufferBlock;

struct CanardRxState
{
  struct CanardRxState* next;
  CanardBufferBlock* buffer_blocks;

  uint64_t timestamp_usec;

  //uint32_t dtid_tt_snid_dnid;
  const uint32_t dtid_tt_snid_dnid;

  uint16_t payload_crc;
  uint16_t payload_len : 10;
  uint16_t transfer_id : 5;
  uint16_t next_toggle : 1;

  uint8_t buffer_head[];
};

/**
 * This structure maintains the current canard instance of this node including the node_ID,
 * a function pointer, should_accept, which the application uses to decide whether to keep this or subsequent frames,
 * a function pointer, on_reception, which hands a completed transfer to the application,
 * the allocator and its blocks,
 * and
 * the CanardRxState list
 */
struct CanardInstance
{
  uint8_t node_id;  // local node

  canardShouldAcceptTransferPtr should_accept; 						// function to decide whether we want this transfer
  canardOnTransferReception on_reception;        					// function we call after rx transfer is complete

  CanardPoolAllocator allocator;									// pool allocator
  CanardPoolAllocatorBlock buffer[CANARD_AVAILABLE_BLOCKS];			// pool blocks

  CanardRxState* rx_states;
  CanardTxQueueItem* tx_queue;
};


/**
 * This structure represents a received transfer for the application.
 * An instance of it is passed to the application via callback when
 * the library receives a new transfer.
 */
struct CanardRxTransfer
{
    /**
     * Timestamp at which the first frame of this transfer was received.
     */
    uint64_t timestamp_usec;

    /**
     * Payload is scattered across three storages:
     *  - Head points to CanardRxState.buffer_head (length of which is up to Canard_PAYLOAD_HEAD_SIZE), or to the
     *    payload field (possibly with offset) of the last received CAN frame.
     *  - Middle is located in the linked list of dynamic blocks.
     *  - Tail points to the payload field (possibly with offset) of the last received CAN frame
     *    (only for multi-frame transfers).
     *
     * The tail offset depends on how much data of the last frame was accommodated in the last allocated
     * block.
     *
     * For single-frame transfers, middle and tail will be NULL, and the head will point at first byte
     * of the payload of the CAN frame.
     *
     * In simple cases it should be possible to get data directly from the head and/or tail pointers.
     * Otherwise it is advised to use canardReadRxTransferPayload().
     */
    const uint8_t*           payload_head;   ///< Always valid, i.e. not NULL.
    CanardBufferBlock* payload_middle; ///< May be NULL if the buffer was not needed. Always NULL for single-frame transfers.
    const uint8_t*           payload_tail;   ///< Last bytes of multi-frame transfers. Always NULL for single-frame transfers.
    uint16_t payload_len;
    uint16_t middle_len;

    /**
     * These fields identify the transfer for the application logic.
     */
    uint16_t data_type_id;                  ///< 0 to 255 for services, 0 to 65535 for messages
    uint8_t transfer_type;                  ///< See @ref CanardTransferType
    uint8_t transfer_id;                    ///< 0 to 31
    uint8_t priority;                       ///< 0 to 31
    uint8_t source_node_id;                 ///< 1 to 127, or 0 if the source is anonymous
};

void canardInit(CanardInstance* out_ins, canardOnTransferReception on_reception);
void canardSetLocalNodeID(CanardInstance* ins, uint8_t self_node_id);
uint8_t canardGetLocalNodeID(const CanardInstance* ins);
int canardBroadcast(CanardInstance* ins, uint16_t data_type_id, uint8_t* inout_transfer_id, 
                              uint8_t priority, const void* payload, uint16_t payload_len);
int canardRequestOrRespond(CanardInstance* ins, uint8_t destination_node_id, uint16_t data_type_id, uint8_t* inout_transfer_id, 
                              uint8_t priority, CanardRequestResponse kind, const void* payload, uint16_t payload_len);
const CanardCANFrame* canardPeekTxQueue(const CanardInstance* ins);
void canardPopTxQueue(CanardInstance* ins);
void canardHandleRxFrame(CanardInstance* ins, const CanardCANFrame* frame, uint64_t timestamp_usec);
void canardCleanupStaleTransfers(CanardInstance* ins, uint64_t timeout_usec, uint64_t current_time_usec);
uint64_t canardReleaseRxTransferPayload(CanardInstance* ins, CanardRxTransfer* transfer);

#ifdef __cplusplus
}
#endif
#endif
