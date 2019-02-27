/*
 * This demo application is distributed under the terms of CC0 (public domain dedication).
 * More info: https://creativecommons.org/publicdomain/zero/1.0/
 */

// This is needed to enable necessary declarations in sys/
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <catch.hpp>
#include <canard.h>

static bool shouldAccept = true;

/**
 * This callback is invoked by the library when a new message or request or response is received.
 */
static void onTransferReceived(CanardInstance* ins,
                               CanardRxTransfer* transfer)
{
    (void)ins;
    (void)transfer;
}

static bool shouldAcceptTransfer(const CanardInstance* ins,
                                 uint64_t* out_data_type_signature,
                                 uint16_t data_type_id,
                                 CanardTransferType transfer_type,
                                 uint8_t source_node_id)
{
    (void)ins;
    (void)out_data_type_signature;
    (void)data_type_id;
    (void)transfer_type;
    (void)source_node_id;
    return shouldAccept;
}

TEST_CASE("canardHandleRxFrame incompatible packet handling, Correctness")
{
    uint8_t canard_memory_pool[1024];
    CanardInstance canard;
    CanardCANFrame frame;
    CanardError err;

    //Setup frame data to be single frame transfer
    frame.data[0] = (1 << 7) | (1 << 6);

    canardInit(&canard, canard_memory_pool, sizeof(canard_memory_pool), onTransferReceived, shouldAcceptTransfer, (void*)&canard);
    shouldAccept = true;

    //Frame with good RTR/ERR/data_len bad EFF
    frame.id = 0;
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_INCOMPATIBLE_PACKET == err);

    //Frame with good EFF/ERR/data_len, bad RTR
    frame.id = 0 | CANARD_CAN_FRAME_RTR | CANARD_CAN_FRAME_EFF;
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_INCOMPATIBLE_PACKET == err);

    //Frame with good EFF/RTR/data_len, bad ERR
    frame.id = 0 | CANARD_CAN_FRAME_ERR | CANARD_CAN_FRAME_EFF;
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_INCOMPATIBLE_PACKET == err);

    //Frame with good EFF/RTR/ERR, bad data_len
    frame.id = 0 | CANARD_CAN_FRAME_EFF;
    frame.data_len = 0;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_INCOMPATIBLE_PACKET == err);

    //Frame with good EFF/RTR/ERR/data_len
    frame.id = 0 | CANARD_CAN_FRAME_EFF;
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);
}

TEST_CASE("canardHandleRxFrame wrong address handling, Correctness")
{
    uint8_t canard_memory_pool[1024];
    CanardInstance canard;
    CanardCANFrame frame;
    CanardError err;

    //Setup frame data to be single frame transfer
    frame.data[0] = (1 << 7) | (1 << 6);

    //Open canard to accept all transfers with a node ID of 20
    canardInit(&canard, canard_memory_pool, sizeof(canard_memory_pool), onTransferReceived, shouldAcceptTransfer, (void*)&canard);
    canardSetLocalNodeID(&canard, 20);
    shouldAccept = true;

    //Send package with ID 24, should not be wanted
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (24 << 8);                          //Set address to 24
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_WRONG_ADDRESS == err);

    //Send package with ID 20, should be OK
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);
}

TEST_CASE("canardHandleRxFrame shouldAccept handling, Correctness")
{
    uint8_t canard_memory_pool[1024];
    CanardInstance canard;
    CanardCANFrame frame;
    CanardError err;

    //Setup frame data to be single frame transfer
    frame.data[0] = (1 << 7) | (1 << 6);

    //Open canard to accept all transfers with a node ID of 20
    canardInit(&canard, canard_memory_pool, sizeof(canard_memory_pool), onTransferReceived, shouldAcceptTransfer, (void*)&canard);
    canardSetLocalNodeID(&canard, 20);

    //Send packet, don't accept
    shouldAccept = false;
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_NOT_WANTED == err);

    //Send packet, accept
    shouldAccept = true;
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);
}

TEST_CASE("canardHandleRxFrame no state handling, Correctness")
{
    uint8_t canard_memory_pool[1024];
    CanardInstance canard;
    CanardCANFrame frame;
    CanardError err;

    shouldAccept = true;

    //Open canard to accept all transfers with a node ID of 20
    canardInit(&canard, canard_memory_pool, sizeof(canard_memory_pool), onTransferReceived, shouldAcceptTransfer, (void*)&canard);
    canardSetLocalNodeID(&canard, 20);

    //Not start or end of packet, should fail
    frame.data[0] = 0;
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_NO_STATE == err);

    //End of packet, should fail
    frame.data[0] = (1 << 6);
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_NO_STATE == err);

    //1 Frame packet, should pass
    frame.data[0] = (1 << 7) | (1 << 6);
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);

    //Send a start packet, should pass
    frame.data[0] = (1 << 7) | 1;                   //Use TID 1
    frame.data[7] = (1 << 7) | 1;                   //Use TID 1

    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;                             //Data length MUST be full packet
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);

    //Send a middle packet, from the same ID, but don't toggle
    frame.data[0] = 0 | 1;
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 1;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_WRONG_TOGGLE == err);

    //Send a middle packet, toggle, but use wrong ID
    frame.data[0] = 0 | 2;
    frame.data[7] = 0 | 2 | (1<<5);
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_UNEXPECTED_TID == err);

    //Send a middle packet, toggle, and use correct ID
    frame.data[0] = 0 | 1;
    frame.data[7] = 0 | 1 | (1<<5);
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);

}

TEST_CASE("canardHandleRxFrame missed start handling, Correctness")
{
    uint8_t canard_memory_pool[1024];
    CanardInstance canard;
    CanardCANFrame frame;
    CanardError err;

    shouldAccept = true;

    //Open canard to accept all transfers with a node ID of 20
    canardInit(&canard, canard_memory_pool, sizeof(canard_memory_pool), onTransferReceived, shouldAcceptTransfer, (void*)&canard);
    canardSetLocalNodeID(&canard, 20);

    //Send a start packet, should pass
    frame.data[0] = (1 << 7) | 1;                   //Use TID 1
    frame.data[7] = (1 << 7) | 1;                   //Use TID 1

    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;                             //Data length MUST be full packet
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);

    //Send a middle packet, toggle, and use correct ID - but timeout
    frame.data[0] = 0 | 1;
    frame.data[7] = 0 | 1 | (1<<5);
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;
    err = canardHandleRxFrame(&canard, &frame, 4000000);
    REQUIRE(-CANARD_ERROR_RX_MISSED_START == err);

    //Send a start packet, should pass
    frame.data[0] = (1 << 7) | 1;                   //Use TID 1
    frame.data[7] = (1 << 7) | 1;                   //Use TID 1

    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;                             //Data length MUST be full packet
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);

    //Send a middle packet, toggle, and use correct ID - but timestamp 0
    frame.data[0] = 0 | 1;
    frame.data[7] = 0 | 1 | (1<<5);
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;
    err = canardHandleRxFrame(&canard, &frame, 0);
    REQUIRE(-CANARD_ERROR_RX_MISSED_START == err);

    //Send a start packet, should pass
    frame.data[0] = (1 << 7) | 1;                   //Use TID 1
    frame.data[7] = (1 << 7) | 1;                   //Use TID 1

    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;                             //Data length MUST be full packet
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(CANARD_OK == err);

    //Send a middle packet, toggle, and use an incorrect TID
    frame.data[0] = 0 | 3;
    frame.data[7] = 0 | 3 | (1<<5);
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 8;
    err = canardHandleRxFrame(&canard, &frame, 0);
    REQUIRE(-CANARD_ERROR_RX_MISSED_START == err);
}

TEST_CASE("canardHandleRxFrame short frame handling, Correctness")
{
    uint8_t canard_memory_pool[1024];
    CanardInstance canard;
    CanardCANFrame frame;
    CanardError err;

    shouldAccept = true;

    //Open canard to accept all transfers with a node ID of 20
    canardInit(&canard, canard_memory_pool, sizeof(canard_memory_pool), onTransferReceived, shouldAcceptTransfer, (void*)&canard);
    canardSetLocalNodeID(&canard, 20);

    //Send a start packet which is short, should fail
    frame.data[0] = (1 << 7) | 1;                   //Use TID 1
    frame.data[1] = (1 << 7) | 1;                   //Use TID 1
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 2;                             //Data length MUST be full packet
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_SHORT_FRAME == err);

    //Send a start packet which is short, should fail
    frame.data[0] = (1 << 7) | 1;                   //Use TID 1
    frame.data[2] = (1 << 7) | 1;                   //Use TID 1
    frame.id = 0 | CANARD_CAN_FRAME_EFF;            //Set EFF
    frame.id |= (1 << 7);                           //Set service bit
    frame.id |= (20 << 8);                          //Set address to 20
    frame.data_len = 3;                             //Data length MUST be full packet
    err = canardHandleRxFrame(&canard, &frame, 1);
    REQUIRE(-CANARD_ERROR_RX_SHORT_FRAME == err);



}


