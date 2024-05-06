// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 OpenCyphal Development Team.

#include "exposed.hpp"
#include "helpers.hpp"
#include "catch.hpp"
#include <cstring>

// clang-tidy mistakenly suggests to avoid C arrays here, which is clearly an error
template <typename P, std::size_t N>
auto ensureAllNullptr(P* const (&arr)[N]) -> bool  // NOLINT
{
    return std::all_of(std::begin(arr), std::end(arr), [](const auto* const x) { return x == nullptr; });
}

TEST_CASE("RxBasic0")
{
    using helpers::Instance;
    using exposed::RxSession;

    Instance              ins;
    CanardRxTransfer      transfer{};
    CanardRxSubscription* subscription = nullptr;

    const auto accept = [&](const std::uint8_t               redundant_iface_index,
                            const CanardMicrosecond          timestamp_usec,
                            const std::uint32_t              extended_can_id,
                            const std::vector<std::uint8_t>& payload) {
        static std::vector<std::uint8_t> payload_storage;
        payload_storage = payload;
        CanardFrame frame{};
        frame.extended_can_id = extended_can_id;
        frame.payload_size    = std::size(payload);
        frame.payload         = payload_storage.data();
        return ins.rxAccept(timestamp_usec, frame, redundant_iface_index, transfer, &subscription);
    };

    ins.getAllocator().setAllocationCeiling(sizeof(RxSession) + 16);  // A session and a 16-byte payload buffer.

    // No subscriptions by default.
    REQUIRE(ins.getMessageSubs().empty());
    REQUIRE(ins.getResponseSubs().empty());
    REQUIRE(ins.getRequestSubs().empty());

    // A valid single-frame transfer for which there is no subscription.
    subscription = nullptr;
    REQUIRE(0 == accept(0, 100'000'000, 0b001'00'0'11'0110011001100'0'0100111, {0b111'00000}));
    REQUIRE(subscription == nullptr);

    // Create a message subscription.
    CanardRxSubscription sub_msg{};
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindMessage, 0b0110011001100, 32, 2'000'000, sub_msg));  // New.
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(&sub_msg == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(0 == ins.rxSubscribe(CanardTransferKindMessage, 0b0110011001100, 16, 1'000'000, sub_msg));  // Replaced.
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(&sub_msg == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(ins.getMessageSubs().at(0) == &sub_msg);
    REQUIRE(ins.getMessageSubs().at(0)->port_id == 0b0110011001100);
    REQUIRE(ins.getMessageSubs().at(0)->extent == 16);
    REQUIRE(ins.getMessageSubs().at(0)->transfer_id_timeout_usec == 1'000'000);
    REQUIRE(ensureAllNullptr(ins.getMessageSubs().at(0)->sessions));
    REQUIRE(ins.getResponseSubs().empty());
    REQUIRE(ins.getRequestSubs().empty());

    // Create a request subscription.
    CanardRxSubscription sub_req{};
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindRequest, 0b0000110011, 20, 3'000'000, sub_req));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(&sub_req == ins.rxGetSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(ins.getMessageSubs().at(0) == &sub_msg);
    REQUIRE(ins.getResponseSubs().empty());
    REQUIRE(ins.getRequestSubs().at(0) == &sub_req);
    REQUIRE(ins.getRequestSubs().at(0)->port_id == 0b0000110011);
    REQUIRE(ins.getRequestSubs().at(0)->extent == 20);
    REQUIRE(ins.getRequestSubs().at(0)->transfer_id_timeout_usec == 3'000'000);
    REQUIRE(ensureAllNullptr(ins.getRequestSubs().at(0)->sessions));

    // Create a response subscription.
    CanardRxSubscription sub_res{};
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindResponse, 0b0000111100, 10, 100'000, sub_res));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(&sub_res == ins.rxGetSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(ins.getMessageSubs().at(0) == &sub_msg);
    REQUIRE(ins.getResponseSubs().at(0) == &sub_res);
    REQUIRE(ins.getResponseSubs().at(0)->port_id == 0b0000111100);
    REQUIRE(ins.getResponseSubs().at(0)->extent == 10);
    REQUIRE(ins.getResponseSubs().at(0)->transfer_id_timeout_usec == 100'000);
    REQUIRE(ensureAllNullptr(ins.getResponseSubs().at(0)->sessions));
    REQUIRE(ins.getRequestSubs().at(0) == &sub_req);

    // Create a second response subscription. It will come before the one we added above due to lower port-ID.
    CanardRxSubscription sub_res2{};
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindResponse, 0b0000000000, 10, 1'000, sub_res2));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(&sub_res2 == ins.rxGetSubscription(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(ins.getMessageSubs().at(0) == &sub_msg);
    REQUIRE(ins.getResponseSubs().at(0) == &sub_res2);
    REQUIRE(ins.getResponseSubs().at(0)->port_id == 0b0000000000);
    REQUIRE(ins.getResponseSubs().at(0)->extent == 10);
    REQUIRE(ins.getResponseSubs().at(0)->transfer_id_timeout_usec == 1'000);
    REQUIRE(ins.getResponseSubs().at(1) == &sub_res);  // The earlier one.
    REQUIRE(ins.getRequestSubs().at(0) == &sub_req);

    // Accepted message.
    subscription = nullptr;
    REQUIRE(1 == accept(0, 100'000'001, 0b001'00'0'11'0110011001100'0'0100111, {0b111'00000}));
    REQUIRE(subscription != nullptr);
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 0);
    REQUIRE(transfer.payload_size == 0);
    REQUIRE(0 == std::memcmp(transfer.payload, "", 0));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 16));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    auto* msg_payload = transfer.payload;  // Will need it later.

    // Provide the space for an extra session and its payload.
    ins.getAllocator().setAllocationCeiling(sizeof(RxSession) * 2 + 16 + 20);

    // Dropped request because the local node does not have a node-ID.
    subscription = nullptr;
    REQUIRE(0 == accept(0, 100'000'002, 0b011'11'0000110011'0011010'0100111, {0b111'00010}));
    REQUIRE(subscription == nullptr);

    // Dropped request because the local node has a different node-ID.
    ins.setNodeID(0b0011010);
    subscription = nullptr;
    REQUIRE(0 == accept(0, 100'000'002, 0b011'11'0000110011'0011011'0100111, {0b111'00011}));
    REQUIRE(subscription == nullptr);

    // Same request accepted now.
    subscription = nullptr;
    REQUIRE(1 == accept(0, 100'000'002, 0b011'11'0000110011'0011010'0100101, {1, 2, 3, 0b111'00100}));
    REQUIRE(subscription != nullptr);
    REQUIRE(subscription->port_id == 0b0000110011);
    REQUIRE(transfer.timestamp_usec == 100'000'002);
    REQUIRE(transfer.metadata.priority == CanardPriorityHigh);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindRequest);
    REQUIRE(transfer.metadata.port_id == 0b0000110011);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100101);
    REQUIRE(transfer.metadata.transfer_id == 4);
    REQUIRE(transfer.payload_size == 3);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03", 3));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 4);  // Two SESSIONS and two PAYLOAD BUFFERS.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (2 * sizeof(RxSession) + 16 + 20));
    REQUIRE(ins.getRequestSubs().at(0)->sessions[0b0100101] != nullptr);

    // Response transfer not accepted because the local node has a different node-ID.
    // There is no dynamic memory available, but it doesn't matter because a rejection does not require allocation.
    subscription = nullptr;
    REQUIRE(0 == accept(0, 100'000'002, 0b100'10'0000110011'0100111'0011011, {10, 20, 30, 0b111'00000}));
    REQUIRE(subscription == nullptr);

    // Response transfer not accepted due to OOM -- can't allocate RX session.
    subscription = nullptr;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY ==
            accept(0, 100'000'003, 0b100'10'0000111100'0011010'0011011, {5, 0b111'00001}));
    REQUIRE(subscription != nullptr);  // Subscription get assigned before error code
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 4);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (2 * sizeof(RxSession) + 16 + 20));

    // Response transfer not accepted due to OOM -- can't allocate the buffer (RX session is allocated OK).
    ins.getAllocator().setAllocationCeiling(3 * sizeof(RxSession) + 16 + 20);
    subscription = nullptr;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY ==
            accept(0, 100'000'003, 0b100'10'0000111100'0011010'0011011, {5, 0b111'00010}));
    REQUIRE(subscription != nullptr);  // Subscription get assigned before error code
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 5);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (3 * sizeof(RxSession) + 16 + 20));

    // Destroy the message subscription and the buffer to free up memory.
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(1 == ins.rxUnsubscribe(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(0 == ins.rxUnsubscribe(CanardTransferKindMessage, 0b0110011001100));  // Repeat, nothing to do.
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 4);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (2 * sizeof(RxSession) + 16 + 20));
    ins.getAllocator().deallocate(msg_payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 3);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (2 * sizeof(RxSession) + 20));

    // Same response accepted now. We have to keep incrementing the transfer-ID though because it's tracked.
    subscription = nullptr;
    REQUIRE(1 == accept(0, 100'000'003, 0b100'10'0000111100'0011010'0011011, {5, 0b111'00011}));
    REQUIRE(subscription != nullptr);
    REQUIRE(subscription->port_id == 0b0000111100);
    REQUIRE(transfer.timestamp_usec == 100'000'003);
    REQUIRE(transfer.metadata.priority == CanardPriorityNominal);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindResponse);
    REQUIRE(transfer.metadata.port_id == 0b0000111100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0011011);
    REQUIRE(transfer.metadata.transfer_id == 3);
    REQUIRE(transfer.payload_size == 1);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x05", 1));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 4);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (2 * sizeof(RxSession) + 10 + 20));

    // Bad frames shall be rejected silently.
    subscription = nullptr;
    REQUIRE(0 == accept(0, 900'000'000, 0b100'10'0000111100'0011010'0011011, {5, 0b110'00011}));
    REQUIRE(subscription == nullptr);
    subscription = nullptr;
    REQUIRE(0 == accept(0, 900'000'001, 0b100'10'0000111100'0011010'0011011, {}));
    REQUIRE(subscription == nullptr);

    // Unsubscribe.
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(1 == ins.rxUnsubscribe(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(0 == ins.rxUnsubscribe(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindRequest, 0b0000110011));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(1 == ins.rxUnsubscribe(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(0 == ins.rxUnsubscribe(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindResponse, 0b0000111100));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(1 == ins.rxUnsubscribe(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(0 == ins.rxUnsubscribe(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindResponse, 0b0000000000));
    // Should not modify output if the subscription is not found.
    CanardRxSubscription* out_sub = &sub_msg;
    REQUIRE(nullptr == ins.rxGetSubscription(CanardTransferKindResponse, 0b0000000000));
    REQUIRE(0 == canardRxGetSubscription(&ins.getInstance(), CanardTransferKindResponse, 0b0000000000, &out_sub));
    REQUIRE(out_sub == &sub_msg);
}

TEST_CASE("RxAnonymous")
{
    using helpers::Instance;
    using exposed::RxSession;

    Instance              ins;
    CanardRxTransfer      transfer{};
    CanardRxSubscription* subscription = nullptr;

    const auto accept = [&](const std::uint8_t               redundant_iface_index,
                            const CanardMicrosecond          timestamp_usec,
                            const std::uint32_t              extended_can_id,
                            const std::vector<std::uint8_t>& payload) {
        static std::vector<std::uint8_t> payload_storage;
        payload_storage = payload;
        CanardFrame frame{};
        frame.extended_can_id = extended_can_id;
        frame.payload_size    = std::size(payload);
        frame.payload         = payload_storage.data();
        return ins.rxAccept(timestamp_usec, frame, redundant_iface_index, transfer, &subscription);
    };

    ins.getAllocator().setAllocationCeiling(16);

    // A valid anonymous transfer for which there is no subscription.
    subscription = nullptr;
    REQUIRE(0 == accept(0, 100'000'000, 0b001'01'0'11'0110011001100'0'0100111, {0b111'00000}));
    REQUIRE(subscription == nullptr);

    // Create a message subscription.
    void* const          my_user_reference = &ins;
    CanardRxSubscription sub_msg{};
    sub_msg.user_reference = my_user_reference;
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindMessage, 0b0110011001100, 16, 2'000'000, sub_msg));  // New.
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(&sub_msg == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));

    // Accepted anonymous message.
    subscription = nullptr;
    REQUIRE(1 == accept(0,
                        100'000'001,
                        0b001'01'0'11'0110011001100'0'0100111,  //
                        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0b111'00000}));
    REQUIRE(subscription != nullptr);
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(subscription->user_reference == my_user_reference);
    REQUIRE(transfer.timestamp_usec == 100'000'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == CANARD_NODE_ID_UNSET);
    REQUIRE(transfer.metadata.transfer_id == 0);
    REQUIRE(transfer.payload_size == 16);  // Truncated.
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10", 0));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The PAYLOAD BUFFER only! No session for anons.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 16);
    REQUIRE(ensureAllNullptr(ins.getMessageSubs().at(0)->sessions));  // No RX states!

    // Anonymous message not accepted because OOM. The transfer shall remain unmodified by the call, so we re-check it.
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY ==
            accept(0, 100'000'001, 0b001'01'0'11'0110011001100'0'0100111, {3, 2, 1, 0b111'00000}));
    REQUIRE(subscription != nullptr);
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == CANARD_NODE_ID_UNSET);
    REQUIRE(transfer.metadata.transfer_id == 0);
    REQUIRE(transfer.payload_size == 16);  // Truncated.
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10", 0));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The PAYLOAD BUFFER only! No session for anons.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 16);
    REQUIRE(ensureAllNullptr(ins.getMessageSubs().at(0)->sessions));  // No RX states!

    // Release the memory.
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 0);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 0);

    // Accepted anonymous message with small payload.
    subscription = nullptr;
    REQUIRE(1 == accept(0, 100'000'001, 0b001'01'0'11'0110011001100'0'0100111, {1, 2, 3, 4, 5, 6, 0b111'00000}));
    REQUIRE(subscription != nullptr);
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == CANARD_NODE_ID_UNSET);
    REQUIRE(transfer.metadata.transfer_id == 0);
    REQUIRE(transfer.payload_size == 6);  // NOT truncated.
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06", 0));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);      // The PAYLOAD BUFFER only! No session for anons.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 6);       // Smaller allocation.
    REQUIRE(ensureAllNullptr(ins.getMessageSubs().at(0)->sessions));  // No RX states!
}

TEST_CASE("RxSubscriptionErrors")
{
    using helpers::Instance;
    Instance             ins;
    CanardRxSubscription sub{};

    const union
    {
        std::uint64_t      bits;
        CanardTransferKind value;
    } kind{std::numeric_limits<std::uint64_t>::max()};

    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxSubscribe(nullptr, CanardTransferKindMessage, 0, 0, 0, &sub));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxSubscribe(&ins.getInstance(), kind.value, 0, 0, 0, &sub));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==
            canardRxSubscribe(&ins.getInstance(), CanardTransferKindMessage, 0, 0, 0, nullptr));

    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxUnsubscribe(nullptr, CanardTransferKindMessage, 0));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxUnsubscribe(&ins.getInstance(), kind.value, 0));

    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxGetSubscription(nullptr, CanardTransferKindMessage, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxGetSubscription(&ins.getInstance(), kind.value, 0, nullptr));

    // These calls should not touch `fake_ptr`!
    //
    CanardRxSubscription  fake_sub{};
    CanardRxSubscription* fake_ptr = &fake_sub;
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==
            canardRxGetSubscription(nullptr, CanardTransferKindMessage, 0, &fake_ptr));
    REQUIRE(fake_ptr == &fake_sub);
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxGetSubscription(&ins.getInstance(), kind.value, 0, &fake_ptr));
    REQUIRE(fake_ptr == &fake_sub);

    CanardFrame frame{};
    frame.payload_size = 1U;
    CanardRxTransfer transfer{};
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxAccept(&ins.getInstance(), 0, &frame, 0, &transfer, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxAccept(nullptr, 0, &frame, 0, &transfer, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxAccept(&ins.getInstance(), 0, nullptr, 0, &transfer, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxAccept(&ins.getInstance(), 0, &frame, 0, nullptr, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardRxAccept(nullptr, 0, nullptr, 0, nullptr, nullptr));
}

TEST_CASE("Issue189")  // https://github.com/OpenCyphal/libcanard/issues/189
{
    using helpers::Instance;
    using exposed::RxSession;

    Instance              ins;
    CanardRxTransfer      transfer{};
    CanardRxSubscription* subscription          = nullptr;
    const std::uint8_t    redundant_iface_index = 0;

    const auto accept = [&](const CanardMicrosecond          timestamp_usec,
                            const std::uint32_t              extended_can_id,
                            const std::vector<std::uint8_t>& payload) {
        static std::vector<std::uint8_t> payload_storage;
        payload_storage = payload;
        CanardFrame frame{};
        frame.extended_can_id = extended_can_id;
        frame.payload_size    = std::size(payload);
        frame.payload         = payload_storage.data();
        return ins.rxAccept(timestamp_usec, frame, redundant_iface_index, transfer, &subscription);
    };

    ins.getAllocator().setAllocationCeiling(sizeof(RxSession) + 50);  // A session and the payload buffer.

    // Create a message subscription.
    CanardRxSubscription sub_msg{};
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindMessage, 0b0110011001100, 50, 1'000'000, sub_msg));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(&sub_msg == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(ins.getMessageSubs().at(0) == &sub_msg);
    REQUIRE(ins.getMessageSubs().at(0)->port_id == 0b0110011001100);
    REQUIRE(ins.getMessageSubs().at(0)->extent == 50);
    REQUIRE(ins.getMessageSubs().at(0)->transfer_id_timeout_usec == 1'000'000);
    REQUIRE(ensureAllNullptr(ins.getMessageSubs().at(0)->sessions));
    REQUIRE(ins.getResponseSubs().empty());
    REQUIRE(ins.getRequestSubs().empty());

    // First, ensure that the reassembler is initialized, by feeding it a valid transfer at least once.
    subscription = nullptr;
    REQUIRE(1 == accept(100'000'001, 0b001'00'0'11'0110011001100'0'0100111, {0x42, 0b111'00000}));
    REQUIRE(subscription != nullptr);
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 0);
    REQUIRE(transfer.payload_size == 1);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x42", 1));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 50));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The payload buffer is gone.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));

    // Next, feed the last frame of another transfer whose TID/TOG match the expected state of the reassembler,
    // and the CRC matches the payload available in the last frame.
    // This frame should be rejected because we didn't observe the first frame of this transfer.
    // This failure mode may occur when the first frame is lost.
    //
    // Here's how we compute the reference value of the transfer CRC:
    //  >>> from pycyphal.transport.commons.crc import CRC16CCITT
    //  >>> CRC16CCITT.new(b'DUCK').value_as_bytes
    //  b'4\xa3'
    subscription = nullptr;
    REQUIRE(0 == accept(100'001'001,  // The result should be zero because the transfer is rejected.
                        0b001'00'0'11'0110011001100'0'0100111,           //
                        {'D', 'U', 'C', 'K', '4', 0xA3, 0b011'00001}));  // SOF=0, EOF=1, TOG=1, TID=1, CRC=0x4A34
    REQUIRE(subscription != nullptr);                                    // Subscription exists.
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);         // The SESSION only.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);

    // Now feed a similar transfer that is not malformed. It should be accepted.
    // Here's how we compute the reference value of the transfer CRC:
    //  >>> from pycyphal.transport.commons.crc import CRC16CCITT
    //  >>> CRC16CCITT.new(b'\x01\x02\x03\x04\x05\x06\x07DUCK').value_as_bytes
    //  b'\xd3\x14'
    subscription = nullptr;
    REQUIRE(0 == accept(100'002'001,  //
                        0b001'00'0'11'0110011001100'0'0100111,
                        {1, 2, 3, 4, 5, 6, 7, 0b101'00010}));
    REQUIRE(subscription != nullptr);  // Subscription exists.
    REQUIRE(1 == accept(100'002'002,   // Accepted!
                        0b001'00'0'11'0110011001100'0'0100111,
                        {'D', 'U', 'C', 'K', 0xD3, 0x14, 0b010'00010}));
    REQUIRE(subscription != nullptr);  // Subscription exists.
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'002'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 2);
    REQUIRE(transfer.payload_size == 11);
    REQUIRE(0 == std::memcmp(transfer.payload,
                             "\x01\x02\x03\x04\x05\x06\x07"
                             "DUCK",
                             11));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 50));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The payload buffer is gone.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));
}

TEST_CASE("Issue212")
{
    using helpers::Instance;
    using exposed::RxSession;

    Instance              ins;
    CanardRxTransfer      transfer{};
    CanardRxSubscription* subscription = nullptr;

    const auto accept = [&](const CanardMicrosecond          timestamp_usec,
                            const std::uint8_t               redundant_iface_index,
                            const std::uint32_t              extended_can_id,
                            const std::vector<std::uint8_t>& payload) {
        static std::vector<std::uint8_t> payload_storage;
        payload_storage = payload;
        CanardFrame frame{};
        frame.extended_can_id = extended_can_id;
        frame.payload_size    = std::size(payload);
        frame.payload         = payload_storage.data();
        return ins.rxAccept(timestamp_usec, frame, redundant_iface_index, transfer, &subscription);
    };

    ins.getAllocator().setAllocationCeiling(sizeof(RxSession) + 50);  // A session and the payload buffer.

    // Create a message subscription with the transfer-ID timeout of just one microsecond.
    CanardRxSubscription sub_msg{};
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindMessage, 0b0110011001100, 50, 1, sub_msg));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(&sub_msg == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(ins.getMessageSubs().at(0) == &sub_msg);
    REQUIRE(ins.getMessageSubs().at(0)->port_id == 0b0110011001100);
    REQUIRE(ins.getMessageSubs().at(0)->extent == 50);
    REQUIRE(ins.getMessageSubs().at(0)->transfer_id_timeout_usec == 1);
    REQUIRE(ensureAllNullptr(ins.getMessageSubs().at(0)->sessions));
    REQUIRE(ins.getResponseSubs().empty());
    REQUIRE(ins.getRequestSubs().empty());

    // Feed a multi-frame transfer with a time delta between its frames larger than the transfer-ID timeout.
    // Here's how we compute the reference value of the transfer CRC:
    //  >>> from pycyphal.transport.commons.crc import CRC16CCITT
    //  >>> CRC16CCITT.new(bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14])).value_as_bytes
    //  b'2\xf8'
    subscription = nullptr;
    REQUIRE(0 == accept(100'000'001,  // first frame
                        1,
                        0b001'00'0'11'0110011001100'0'0100111,
                        {1, 2, 3, 4, 5, 6, 7, 0b101'00010}));
    REQUIRE(0 == accept(101'000'001,  // second frame
                        1,
                        0b001'00'0'11'0110011001100'0'0100111,
                        {8, 9, 10, 11, 12, 13, 14, 0b000'00010}));
    REQUIRE(1 == accept(102'000'002,  // third and last frame
                        1,
                        0b001'00'0'11'0110011001100'0'0100111,
                        {0x32, 0xF8, 0b011'00010}));
    REQUIRE(subscription != nullptr);  // Subscription exists.
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 2);
    REQUIRE(transfer.payload_size == 14);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E", 14));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 50));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The payload buffer is gone.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));

    // Similar but the reassembler should NOT switch to the other transport.
    REQUIRE(0 == accept(110'000'001,  // first frame, transport #1
                        1,
                        0b001'00'0'11'0110011001100'0'0100111,
                        {1, 2, 3, 4, 5, 6, 7, 0b101'00011}));
    REQUIRE(0 == accept(111'000'001,  // first frame, transport #0
                        0,
                        0b001'00'0'11'0110011001100'0'0100111,
                        {1, 2, 3, 4, 5, 6, 7, 0b101'00011}));
    REQUIRE(0 == accept(112'000'001,  // second frame, transport #1
                        1,
                        0b001'00'0'11'0110011001100'0'0100111,
                        {8, 9, 10, 11, 12, 13, 14, 0b000'00011}));
    REQUIRE(1 == accept(113'000'002,  // third and last frame, transport #1
                        1,
                        0b001'00'0'11'0110011001100'0'0100111,
                        {0x32, 0xF8, 0b011'00011}));
    REQUIRE(subscription != nullptr);  // Subscription exists.
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 110'000'001);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 3);
    REQUIRE(transfer.payload_size == 14);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E", 14));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 50));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The payload buffer is gone.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));
}

TEST_CASE("RxFixedTIDWithSmallTimeout")
{
    using helpers::Instance;
    using exposed::RxSession;

    Instance              ins;
    CanardRxTransfer      transfer{};
    CanardRxSubscription* subscription = nullptr;

    const auto accept = [&](const CanardMicrosecond          timestamp_usec,
                            const std::uint32_t              extended_can_id,
                            const std::vector<std::uint8_t>& payload) {
        static std::vector<std::uint8_t> payload_storage;
        payload_storage = payload;
        CanardFrame frame{};
        frame.extended_can_id = extended_can_id;
        frame.payload_size    = std::size(payload);
        frame.payload         = payload_storage.data();
        return ins.rxAccept(timestamp_usec, frame, 0, transfer, &subscription);
    };

    ins.getAllocator().setAllocationCeiling(sizeof(RxSession) + 50);  // A session and the payload buffer.

    // Create a message subscription with the transfer-ID timeout of just five microseconds.
    CanardRxSubscription sub_msg{};
    REQUIRE(0 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(1 == ins.rxSubscribe(CanardTransferKindMessage, 0b0110011001100, 50, 5, sub_msg));
    REQUIRE(1 == ins.rxHasSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(&sub_msg == ins.rxGetSubscription(CanardTransferKindMessage, 0b0110011001100));
    REQUIRE(ins.getMessageSubs().at(0) == &sub_msg);
    REQUIRE(ins.getMessageSubs().at(0)->port_id == 0b0110011001100);
    REQUIRE(ins.getMessageSubs().at(0)->extent == 50);
    REQUIRE(ins.getMessageSubs().at(0)->transfer_id_timeout_usec == 5);
    REQUIRE(ensureAllNullptr(ins.getMessageSubs().at(0)->sessions));
    REQUIRE(ins.getResponseSubs().empty());
    REQUIRE(ins.getRequestSubs().empty());

    // Feed a valid multi-frame transfer.
    // Here's how we compute the reference value of the transfer CRC:
    //  >>> from pycyphal.transport.commons.crc import CRC16CCITT
    //  >>> CRC16CCITT.new(bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14])).value_as_bytes
    //  b'2\xf8'
    REQUIRE(0 == accept(100'000'000,  // first frame
                        0b001'00'0'11'0110011001100'0'0100111,
                        {1, 2, 3, 4, 5, 6, 7, 0b101'00000}));
    REQUIRE(0 == accept(100'000'001,  // second frame, one us later
                        0b001'00'0'11'0110011001100'0'0100111,
                        {8, 9, 10, 11, 12, 13, 14, 0b000'00000}));
    REQUIRE(1 == accept(100'000'020,  // third and last frame, large delay greater than the timeout
                        0b001'00'0'11'0110011001100'0'0100111,
                        {0x32, 0xF8, 0b011'00000}));
    REQUIRE(subscription != nullptr);  // Subscription exists.
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'000);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 0);
    REQUIRE(transfer.payload_size == 14);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E", 14));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 50));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The payload buffer is gone.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));

    // Another transfer with the same transfer-ID but past the transfer-ID timeout; it should be accepted.
    REQUIRE(0 == accept(100'000'100,  // first frame
                        0b001'00'0'11'0110011001100'0'0100111,
                        {1, 2, 3, 4, 5, 6, 7, 0b101'00000}));
    REQUIRE(1 == accept(100'000'101,  // third and last frame
                        0b001'00'0'11'0110011001100'0'0100111,
                        {8, 0x47, 0x92, 0b010'00000}));
    REQUIRE(subscription != nullptr);  // Subscription exists.
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'100);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 0);  // same
    REQUIRE(transfer.payload_size == 8);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06\x07\x08", 8));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 50));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The payload buffer is gone.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));

    // Same but now single-frame.
    REQUIRE(1 == accept(100'000'200,  // the only frame
                        0b001'00'0'11'0110011001100'0'0100111,
                        {1, 2, 3, 4, 5, 6, 7, 0b111'00000}));
    REQUIRE(subscription != nullptr);  // Subscription exists.
    REQUIRE(subscription->port_id == 0b0110011001100);
    REQUIRE(transfer.timestamp_usec == 100'000'200);
    REQUIRE(transfer.metadata.priority == CanardPriorityImmediate);
    REQUIRE(transfer.metadata.transfer_kind == CanardTransferKindMessage);
    REQUIRE(transfer.metadata.port_id == 0b0110011001100);
    REQUIRE(transfer.metadata.remote_node_id == 0b0100111);
    REQUIRE(transfer.metadata.transfer_id == 0);  // same
    REQUIRE(transfer.payload_size == 7);
    REQUIRE(0 == std::memcmp(transfer.payload, "\x01\x02\x03\x04\x05\x06\x07", 7));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 2);  // The SESSION and the PAYLOAD BUFFER.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == (sizeof(RxSession) + 50));
    REQUIRE(ins.getMessageSubs().at(0)->sessions[0b0100111] != nullptr);
    ins.getAllocator().deallocate(transfer.payload);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);  // The payload buffer is gone.
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == sizeof(RxSession));
}
