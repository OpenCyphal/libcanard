// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "internals.hpp"
#include "helpers.hpp"

TEST_CASE("TxMessageSingleFrame")
{
    helpers::Instance ins;

    REQUIRE(CANARD_NODE_ID_UNSET == ins.getNodeID());
    REQUIRE(CANARD_MTU_CAN_FD == ins.getMTU());
    REQUIRE(0 == ins.getAllocator().getNumAllocatedFragments());
    REQUIRE(nullptr == ins.getTxQueueRoot());
    REQUIRE(0 == ins.getTxQueueLength());
}
