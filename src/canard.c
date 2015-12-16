#include "canard.h"
#include "canard_internals.h"
// #include <inttypes.h>


#define CANARD_MAKE_TRANSFER_DESCRIPTOR(data_type_id, transfer_type, \
                                        src_node_id, dst_node_id) \
    ((data_type_id) | ((transfer_type) << 16) | \
     ((src_node_id) << 18) | ((dst_node_id) << 25))

struct CanardTxQueueItem
{
  CanardTxQueueItem* next;
  CanardCANFrame frame;
};

/**
 *  API functions
 */

/**
 * Initializes the library state.
 * Local node ID will be set to zero, i.e. the node will be anonymous.
 */
void canardInit(CanardInstance* out_ins, canardOnTransferReception on_reception) {
  out_ins->node_id = CANARD_BROADCAST_NODE_ID;
  out_ins->on_reception = on_reception;
  out_ins->rx_states = NULL;
  out_ins->tx_queue = NULL;
  canardInitPoolAllocator(&out_ins->allocator, out_ins->buffer, CANARD_AVAILABLE_BLOCKS);
}

/**
 * Assigns a new node ID value to the current node.
 */
void canardSetLocalNodeID(CanardInstance* ins, uint8_t self_node_id) {
  ins->node_id = self_node_id;
}

/**
 * Returns node ID of the local node.
 * Returns zero if the node ID has not been set.
 */
uint8_t canardGetLocalNodeID(const CanardInstance* ins) {
  return ins->node_id;
}

/**
 * Sends a broadcast transfer.
 * If the node is in passive mode, only single frame transfers will be allowed.
 */
 int canardBroadcast(CanardInstance* ins,
                      uint16_t data_type_id,
                      uint8_t* inout_transfer_id,
                      uint8_t priority,
                      const uint8_t* payload,
                      uint16_t payload_len) {
  if (payload == NULL)
  {
      return -1;
  }
  if (canardGetLocalNodeID(ins) == 0 && payload_len > 7) {
    return -1;
  }
  if (priority > 31)
  {
      return -1;
  }

  const uint32_t can_id = ((uint32_t)priority << 24) | ((uint32_t)data_type_id << 8) | (uint32_t)canardGetLocalNodeID(ins);
  //single frame transfer
  if (payload_len < CANARD_CAN_FRAME_MAX_DATA_LEN) {  

    CanardTxQueueItem* queue_item = canardCreateTxItem(&ins->allocator);

    memcpy(queue_item->frame.data, payload, payload_len);

    queue_item->frame.data_len = payload_len+1;

    queue_item->frame.data[payload_len] = 0xC0 | (*inout_transfer_id & 31);

    queue_item->frame.id = can_id;

    *inout_transfer_id += 1;
    if (*inout_transfer_id >= 32) {
      *inout_transfer_id = 0;
    }

    canardPushTxQueue(ins, queue_item);

  } else if (payload_len >= CANARD_CAN_FRAME_MAX_DATA_LEN){
    //multiframe transfer
    uint8_t i=2, data_index = 0, toggle = 0, sot_eot = 0x80;

    CanardTxQueueItem* frame;

    while (payload_len - data_index != 0) {
      frame = canardCreateTxItem(&ins->allocator);

      if (data_index == 0) {
        frame->frame.data[0] = 0xFF;
        frame->frame.data[1] = 0XFF;
        i = 2;
      } else {
        i = 0;
      }

      for (; i<7 && data_index<payload_len; i++, data_index++) {
        frame->frame.data[i] = payload[data_index];
      }

      sot_eot = (data_index==payload_len) ? 0x40: sot_eot;

      *inout_transfer_id += 1;
      if (*inout_transfer_id >= 32) {
        *inout_transfer_id = 0;
      }

      frame->frame.data[i] = sot_eot | (toggle << 5) | (*inout_transfer_id & 31);
      frame->frame.id = can_id;
      frame->frame.data_len = i+1;
      canardPushTxQueue(ins,frame);

      toggle ^= 1;
      sot_eot = 0;
    }
  }
  return 1;
}

/**
 * Returns a pointer to the top priority frame in the TX queue.
 * Returns NULL if the TX queue is empty.
 */
const CanardCANFrame* canardPeekTxQueue(const CanardInstance* ins) {
  if (ins->tx_queue == NULL) {
    return NULL;
  }
  return &ins->tx_queue->frame;
}

/**
 * Removes the top priority frame from the TX queue.
 */
void canardPopTxQueue(CanardInstance* ins) {
  CanardTxQueueItem* item = ins->tx_queue;
  ins->tx_queue = item->next;
  canardFreeBlock(&ins->allocator, item);
}

/**
 * Processes a received CAN frame with a timestamp.
 */
void canardHandleRxFrame(CanardInstance* ins, const CanardCANFrame* frame, uint64_t timestamp_usec) {
  
  uint8_t transfer_type = canardTransferType(frame->id);
  uint8_t priority = CANARD_PRIORITY_FROM_ID(frame->id);
  uint8_t source_node_id = CANARD_SOURCE_ID_FROM_ID(frame->id);
  uint8_t destination_node_id = (transfer_type == CanardTransferTypeBroadcast) ? 0 : CANARD_DEST_ID_FROM_ID(frame->id);
  uint16_t data_type_id = canardDataType(frame->id);
  uint32_t transfer_descriptor = CANARD_MAKE_TRANSFER_DESCRIPTOR(data_type_id,transfer_type,source_node_id,destination_node_id);
  
  CanardRxState* rxstate;
  unsigned char tail_byte = frame->data[frame->data_len-1];
  rxstate = canardRxStateTraversal(ins, frame, transfer_descriptor);

  if (IS_START_OF_TRANSFER(tail_byte) && IS_END_OF_TRANSFER(tail_byte)) {  // single frame transfer

    rxstate->timestamp_usec = timestamp_usec;
    CanardRxTransfer rxtransfer = {
    .payload_head = frame->data,
    .payload_len = frame->data_len-1,
    .data_type_id = data_type_id,
    .transfer_type = transfer_type,
    .priority = priority,
    .source_node_id = source_node_id };
    ins->on_reception(ins,&rxtransfer);

  } else if (IS_START_OF_TRANSFER(tail_byte) && !IS_END_OF_TRANSFER(tail_byte)) {  // beginning of multi frame transfer
    //take off the crc and store the payload
    rxstate->timestamp_usec = timestamp_usec;

    canardBufferBlockPushBytes(&ins->allocator, rxstate, frame->data+2, frame->data_len-3);

  } else if (!IS_START_OF_TRANSFER(tail_byte) && !IS_END_OF_TRANSFER(tail_byte)) {

    canardBufferBlockPushBytes(&ins->allocator, rxstate, frame->data, frame->data_len-1);

  } else { 
    CanardRxTransfer rxtransfer = {
    .timestamp_usec = timestamp_usec,
    .payload_head = rxstate->buffer_head,
    .payload_middle = rxstate->buffer_blocks,
    .payload_tail = frame->data,
    .payload_len = rxstate->payload_len+frame->data_len-1,
    .data_type_id = data_type_id,
    .transfer_type = transfer_type,
    .priority = priority,
    .source_node_id = source_node_id };

    cleanCanardRxState(rxstate);

    ins->on_reception(ins,&rxtransfer);
  }
}


/**
 * This function can be invoked by the application to release pool blocks that are used
 * to store the payload of this transfer
 */
uint64_t canardReleaseRxTransferPayload(CanardInstance* ins, CanardRxTransfer* transfer) {
  CanardBufferBlock *block, *temp = transfer->payload_middle;
  while (block != NULL) {
    block = temp;
    temp = block->next;
    canardFreeBlock(&ins->allocator, block);
  }
  transfer->payload_middle = NULL;
  transfer->payload_head = NULL;
  transfer->payload_tail = NULL;
  transfer->payload_len = 0;
  return 0;
}

/**
 *  internal (static functions)
 *
 *
 */

/**
 * Puts frame on on the TX queue. Higher priority placed first
 */
CANARD_INTERNAL void canardPushTxQueue(CanardInstance* ins, CanardTxQueueItem* item) {
  if (ins->tx_queue == NULL) {
    ins->tx_queue = item;
    return;
  }
  CanardTxQueueItem* queue = ins->tx_queue;
  CanardTxQueueItem* previous = ins->tx_queue;
  while (queue != NULL) {
    if (priorityHigherThan(queue->frame.id, item->frame.id)) {
      if (queue == ins->tx_queue) {
        item->next = queue;
        ins->tx_queue = item;
      } else {
        previous->next = item;
        item->next = queue;
      }
      return;
    } else {
      if (queue->next == NULL) {
        queue->next = item;
        return;
      } else {
        previous = queue;
        queue = queue->next;
      }
    }
  }
}

/**
 * creates new tx queue item from allocator
 */
CANARD_INTERNAL CanardTxQueueItem *canardCreateTxItem(CanardPoolAllocator* allocator) {
  CanardTxQueueItem init = {0};
  CanardTxQueueItem* item = (CanardTxQueueItem*)canardAllocateBlock(allocator);

  memcpy(item, &init, sizeof *item);

  return item;
}

/**
 * returns true if priority of rhs is higher than id
 */
CANARD_INTERNAL bool priorityHigherThan(uint32_t rhs, uint32_t id) {
  const uint32_t clean_id     = id    & CANARD_EXT_ID_MASK;
  const uint32_t rhs_clean_id = rhs   & CANARD_EXT_ID_MASK;
  /*
   * STD vs EXT - if 11 most significant bits are the same, EXT loses.
   */
  bool ext     = id     & CANARD_CAN_FRAME_EFF;
  bool rhs_ext = rhs & CANARD_CAN_FRAME_EFF;
  if (ext != rhs_ext)
  {
    uint32_t arb11     = ext     ? (clean_id >> 18)     : clean_id;
    uint32_t rhs_arb11 = rhs_ext ? (rhs_clean_id >> 18) : rhs_clean_id;
    if (arb11 != rhs_arb11)
    {
        return arb11 < rhs_arb11;
    }
    else
    {
        return rhs_ext;
    }
  }

  /*
   * RTR vs Data frame - if frame identifiers and frame types are the same, RTR loses.
   */
  bool rtr     = id     & CANARD_CAN_FRAME_RTR;
  bool rhs_rtr = rhs & CANARD_CAN_FRAME_RTR;
  if (clean_id == rhs_clean_id && rtr != rhs_rtr)
  {
    return rhs_rtr;
  }

  /*
   * Plain ID arbitration - greater value loses.
   */
  return clean_id < rhs_clean_id;
}

/**
 * preps the rx state for the next transfer. does not delete the state
 */
CANARD_INTERNAL void cleanCanardRxState(CanardRxState* state) {
  state->buffer_blocks = NULL;
  state->payload_len = 0;
  state->transfer_id = 0;
  state->next_toggle = 0;
}

/**
 * returns data type from id
 */
CANARD_INTERNAL uint16_t canardDataType(uint32_t id) {
  uint8_t transfer_type = canardTransferType(id);
  if (transfer_type == CanardTransferTypeBroadcast) {
    return (uint16_t)CANARD_MSG_TYPE_FROM_ID(id);
  } else {
    return (uint16_t)CANARD_SRV_TYPE_FROM_ID(id);
  }
}

/**
 * returns transfer type from id
 */
CANARD_INTERNAL uint8_t canardTransferType(uint32_t id) {
  uint8_t is_service= (uint8_t)CANARD_SERVICE_NOT_MSG_FROM_ID(id);
  if (is_service == 0) {
    return CanardTransferTypeBroadcast;
  } else if (CANARD_REQUEST_NOT_RESPONSE_FROM_ID(id) == 1) {
    return CanardTransferTypeRequest;
  } else {
    return CanardTransferTypeResponse;
  }
}

/**
 *  CanardRxState functions
 */

/**
 * Traverses the list of CanardRxState's and returns a pointer to the CanardRxState 
 * with either the Id or a new one at the end
 */
CANARD_INTERNAL CanardRxState *canardRxStateTraversal(CanardInstance* ins, const CanardCANFrame* frame, uint32_t transfer_descriptor) {
  CanardRxState* states = ins->rx_states;

  if (states==NULL) {           //initialize CanardRxStates
    states = canardCreateRxState(&ins->allocator, transfer_descriptor);
    ins->rx_states = states;
    return states;
  }

  states = canardFindRxState(states, transfer_descriptor);
  if (states != NULL) {
    return states;
  } else {
    return canardAppendRxState(ins, transfer_descriptor);
  }
}

/**
 * returns pointer to the rx state of transfer descriptor or null if not found
 */
CANARD_INTERNAL CanardRxState *canardFindRxState(CanardRxState* state, uint32_t transfer_descriptor) {
  while(state != NULL) {
    if(state->dtid_tt_snid_dnid == transfer_descriptor) {
      return state;
    }
    state = state->next;
  }
  return NULL;
}

/**
 * appends rx state to the canard instance rx_states
 */
CANARD_INTERNAL CanardRxState *canardAppendRxState(CanardInstance* ins, uint32_t transfer_descriptor) {
  CanardRxState* states = ins->rx_states;
  static int i = 1;
  if(states->next == NULL) {
    states->next = canardCreateRxState(&ins->allocator, transfer_descriptor);
    return states->next;
  }

  while(states->next != NULL) {
    states = states->next;
  }
  i++;
  states->next = canardCreateRxState(&ins->allocator, transfer_descriptor);
  return states->next;
}

CANARD_INTERNAL CanardRxState *canardCreateRxState(CanardPoolAllocator* allocator, uint32_t transfer_descriptor) {
  CanardRxState init = {.next = NULL, .buffer_blocks = NULL, .dtid_tt_snid_dnid = transfer_descriptor};
  CanardRxState* state = (CanardRxState *)canardAllocateBlock(allocator);

  memcpy(state, &init, sizeof *state);

  return state;
}

/**
 *  CanardBufferBlock functions
 */

 /**
 * pushes data into the rx state. Fills the buffer head, then appends data to buffer blocks
 */
CANARD_INTERNAL void canardBufferBlockPushBytes(CanardPoolAllocator* allocator, CanardRxState* state, const uint8_t* data, uint8_t data_len) {
  uint8_t data_index = 0;
  uint8_t i;

  // if head is not full, add data to head
  if (CANARD_RX_PAYLOAD_HEAD_SIZE - state->payload_len > 0) {
    for (i=state->payload_len; i<CANARD_RX_PAYLOAD_HEAD_SIZE && data_index<data_len; i++, data_index++) {
      state->buffer_head[i] = data[data_index];
    }
    if (data_index >= data_len-1) {
      state->payload_len += data_len;
      return;
    }
  } //head is full.

  uint8_t num_buffer_blocks = (((state->payload_len + data_len) - CANARD_RX_PAYLOAD_HEAD_SIZE) / CANARD_BUFFER_BLOCK_DATA_SIZE)+1;
  uint8_t index_at_nth_block = ((state->payload_len) - CANARD_RX_PAYLOAD_HEAD_SIZE) % CANARD_BUFFER_BLOCK_DATA_SIZE;
    
  //get to current block
  CanardBufferBlock* block;
  uint8_t nth_block = 1;
  if (state->buffer_blocks == NULL) {
    state->buffer_blocks = canardCreateBufferBlock(allocator);
    block = state->buffer_blocks;
  } else {
    block = state->buffer_blocks;
    while (block->next != NULL) {
      nth_block++;
      block = block->next;
    }
    if (num_buffer_blocks > nth_block && index_at_nth_block == 0) {
      block->next = canardCreateBufferBlock(allocator);
      block = block->next;
      nth_block++;
    }
  }

  // add data to current block until it becomes full, add new block if necessary
  while (data_index < data_len) {
    for (i=index_at_nth_block; i<CANARD_BUFFER_BLOCK_DATA_SIZE && data_index<data_len; i++, data_index++) {
      block->data[i] = data[data_index];
    }
    if (data_index < data_len) {
      block->next = canardCreateBufferBlock(allocator);
      block = block->next;
      index_at_nth_block = 0;
    }
  }
  state->payload_len += data_len;
  return;
}

/**
 * creates new buffer block
 */
CANARD_INTERNAL CanardBufferBlock *canardCreateBufferBlock(CanardPoolAllocator* allocator) {
  CanardBufferBlock* block = (CanardBufferBlock *)canardAllocateBlock(allocator);
  block->next = NULL;
  return block;
}

/**
 *  Pool Allocator functions
 */

CANARD_INTERNAL void canardInitPoolAllocator(CanardPoolAllocator* allocator, CanardPoolAllocatorBlock* buf, unsigned int buf_len)
{
    unsigned int current_index = 0;
    CanardPoolAllocatorBlock** current_block = &(allocator->free_list);
    while (current_index < buf_len)
    {
        *current_block = &buf[current_index];
        current_block = &((*current_block)->next);
        current_index++;
    }
    *current_block = NULL;
}

CANARD_INTERNAL void* canardAllocateBlock(CanardPoolAllocator* allocator)
{
    /* Check if there are any blocks available in the free list. */
    if (allocator->free_list == NULL)
    {
        return NULL;
    }

    /* Take first available block and prepares next block for use. */
    void* result = allocator->free_list;
    allocator->free_list = allocator->free_list->next;

    return result;
}

CANARD_INTERNAL void canardFreeBlock(CanardPoolAllocator* allocator, void* p)
{
    CanardPoolAllocatorBlock* block = (CanardPoolAllocatorBlock*)p;

    block->next = allocator->free_list;
    allocator->free_list = block;
}
