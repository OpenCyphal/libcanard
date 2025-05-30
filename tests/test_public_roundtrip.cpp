// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 OpenCyphal Development Team.

#include "helpers.hpp"
#include "exposed.hpp"
#include "catch.hpp"

#include <canard.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>

TEST_CASE("RoundtripSimple")
{
    using helpers::getRandomNatural;

    struct TxState
    {
        CanardTransferKind   transfer_kind{};
        CanardPriority       priority{};
        CanardPortID         port_id{};
        std::size_t          extent{};
        CanardTransferID     transfer_id{};
        CanardRxSubscription subscription{};
    };

    helpers::Instance ins_rx;
    ins_rx.setNodeID(111);

    const auto get_random_priority = []() { return getRandomNatural<CanardPriority>(CANARD_PRIORITY_MAX + 1U); };
    std::array<TxState, 6> tx_states{
        TxState{CanardTransferKindMessage, get_random_priority(), 8191, 1000},
        TxState{CanardTransferKindMessage, get_random_priority(), 511, 0},
        TxState{CanardTransferKindMessage, get_random_priority(), 0, 13},
        TxState{CanardTransferKindRequest, get_random_priority(), 511, 900},
        TxState{CanardTransferKindRequest, get_random_priority(), 0, 0},
        TxState{CanardTransferKindResponse, get_random_priority(), 0, 1},
    };
    std::size_t rx_worst_case_memory_consumption = 0;
    for (auto& s : tx_states)
    {
        REQUIRE(1 == ins_rx.rxSubscribe(s.transfer_kind,
                                        s.port_id,
                                        s.extent,
                                        CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                                        s.subscription));
        // The true worst case is 128 times larger, but there is only one transmitting node.
        rx_worst_case_memory_consumption += sizeof(exposed::RxSession) + s.extent;
    }
    ins_rx.getAllocator().setAllocationCeiling(rx_worst_case_memory_consumption);  // This is guaranteed to be enough.

    helpers::Instance ins_tx;
    helpers::TxQueue  que_tx(1024UL * 1024U * 1024U, CANARD_MTU_CAN_FD, ins_tx.makeCanardMemoryResource());
    ins_tx.setNodeID(99);
    ins_tx.getAllocator().setAllocationCeiling(1024UL * 1024U * 1024U);

    using PayloadPtr = std::unique_ptr<std::uint8_t[]>;  // NOLINT
    using Pending    = std::tuple<CanardTransferMetadata, std::size_t, PayloadPtr>;
    std::unordered_map<CanardMicrosecond, Pending> pending_transfers;

    std::atomic<CanardMicrosecond> transfer_counter      = 0;
    std::atomic<std::uint64_t>     frames_in_flight      = 0;
    std::uint64_t                  peak_frames_in_flight = 0;

    std::mutex        lock;
    std::atomic<bool> keep_going = true;

    const auto writer_thread_fun = [&]() {
        while (keep_going)
        {
            auto& st = tx_states.at(getRandomNatural<std::size_t>(std::size(tx_states)));

            // Generate random payload. The size may be larger than expected to test the implicit truncation rule.
            const auto payload_size = getRandomNatural<std::size_t>(st.extent + 1024U);
            PayloadPtr payload{new std::uint8_t[payload_size]};
            std::generate_n(payload.get(), payload_size, [&]() { return getRandomNatural<std::uint8_t>(256U); });

            // Generate the transfer.
            const CanardMicrosecond timestamp_usec = transfer_counter++;
            CanardTransferMetadata  tran{};
            tran.priority      = st.priority;
            tran.transfer_kind = st.transfer_kind;
            tran.port_id       = st.port_id;
            tran.remote_node_id =
                (tran.transfer_kind == CanardTransferKindMessage) ? CANARD_NODE_ID_UNSET : ins_rx.getNodeID();
            tran.transfer_id = (st.transfer_id++) & CANARD_TRANSFER_ID_MAX;

            // Use a random MTU.
            que_tx.setMTU(getRandomNatural<std::size_t>(256U));

            // Push the transfer.
            bool sleep = false;
            {
                const std::lock_guard locker(lock);
                const auto            result =
                    que_tx.push(&ins_tx.getInstance(), timestamp_usec, tran, {payload_size, payload.get()});
                if (result > 0)
                {
                    pending_transfers.emplace(timestamp_usec, Pending{tran, payload_size, std::move(payload)});
                    frames_in_flight += static_cast<std::uint64_t>(result);
                    peak_frames_in_flight = std::max<std::uint64_t>(peak_frames_in_flight, frames_in_flight);
                }
                else
                {
                    if (result != -CANARD_ERROR_OUT_OF_MEMORY)
                    {
                        // Can't use REQUIRE because it is not thread-safe.
                        throw std::logic_error("Unexpected result: " + std::to_string(result));
                    }
                    sleep = true;
                }
            }
            if (sleep)
            {
                std::cout << "TX OOM" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    };

    std::thread writer_thread(writer_thread_fun);

    std::ofstream log_file("roundtrip_frames.log", std::ios::trunc | std::ios::out);
    REQUIRE(log_file.good());

    try
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (true)
        {
            CanardTxQueueItem* ti = nullptr;
            {
                const std::lock_guard locker(lock);
                ti = que_tx.pop(que_tx.peek());
                if (ti != nullptr)
                {
                    REQUIRE(frames_in_flight > 0);
                    --frames_in_flight;
                }
            }

            if (ti != nullptr)
            {
                const auto tail = static_cast<const std::uint8_t*>(ti->frame.payload.data)[ti->frame.payload.size - 1U];
                log_file << ti->tx_deadline_usec << " "                                                              //
                         << std::hex << std::setfill('0') << std::setw(8) << ti->frame.extended_can_id               //
                         << " [" << std::dec << std::setfill(' ') << std::setw(2) << ti->frame.payload.size << "] "  //
                         << (static_cast<bool>(tail & 128U) ? 'S' : ' ')                                             //
                         << (static_cast<bool>(tail & 64U) ? 'E' : ' ')                                              //
                         << (static_cast<bool>(tail & 32U) ? 'T' : ' ')                                              //
                         << " " << static_cast<std::uint16_t>(tail & 31U)                                            //
                         << '\n';

                CanardRxTransfer      transfer{};
                CanardRxSubscription* subscription = nullptr;
                const CanardFrame frame = {ti->frame.extended_can_id, {ti->frame.payload.size, ti->frame.payload.data}};
                const std::int8_t result = ins_rx.rxAccept(ti->tx_deadline_usec, frame, 0, transfer, &subscription);
                REQUIRE(0 == ins_rx.rxAccept(ti->tx_deadline_usec,
                                             frame,
                                             1,
                                             transfer,
                                             &subscription));  // Redundant interface will never be used here.
                if (result == 1)
                {
                    Pending reference{};  // Fetch the reference transfer from the list of pending.
                    {
                        const std::lock_guard locker(lock);
                        const auto            pt_it = pending_transfers.find(transfer.timestamp_usec);
                        REQUIRE(pt_it != pending_transfers.end());
                        reference = std::move(pt_it->second);
                        pending_transfers.erase(pt_it);
                    }
                    const auto [ref_meta, ref_payload_size, ref_payload] = std::move(reference);

                    REQUIRE(transfer.metadata.priority == ref_meta.priority);
                    REQUIRE(transfer.metadata.transfer_kind == ref_meta.transfer_kind);
                    REQUIRE(transfer.metadata.port_id == ref_meta.port_id);
                    REQUIRE(transfer.metadata.remote_node_id == ins_tx.getNodeID());
                    REQUIRE(transfer.metadata.transfer_id == ref_meta.transfer_id);
                    // The payload size is not checked because the variance is huge due to padding and truncation.
                    if (transfer.payload.data != nullptr)
                    {
                        REQUIRE(0 == std::memcmp(transfer.payload.data,
                                                 ref_payload.get(),
                                                 std::min(transfer.payload.size, ref_payload_size)));
                    }
                    else
                    {
                        REQUIRE(transfer.payload.size == 0U);
                        REQUIRE(transfer.payload.allocated_size == 0U);
                    }

                    ins_rx.getAllocator().deallocate(transfer.payload.data, transfer.payload.allocated_size);
                }
                else
                {
                    REQUIRE(result == 0);
                }
            }
            else
            {
                if (!keep_going)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            {
                const std::lock_guard locker(lock);
                que_tx.freeItem(ins_tx, ti);
            }

            if (std::chrono::steady_clock::now() > deadline)
            {
                keep_going = false;
            }
        }
    }
    catch (...)
    {
        keep_going = false;
        writer_thread.detach();
        throw;
    }

    writer_thread.join();

    REQUIRE(0 == frames_in_flight);

    std::cout << "TOTAL TRANSFERS: " << transfer_counter << "; OF THEM LOST: " << std::size(pending_transfers)
              << std::endl;
    std::cout << "PEAK FRAMES IN FLIGHT: " << peak_frames_in_flight << std::endl;

    std::size_t i = 0;
    for (auto& [k, v] : pending_transfers)
    {
        const auto [ref_meta, ref_payload_size, ref_payload] = std::move(v);
        std::cout << "#" << i++ << "/" << std::size(pending_transfers) << ":"        //
                  << " ts=" << k                                                     //
                  << " prio=" << static_cast<std::uint16_t>(ref_meta.priority)       //
                  << " kind=" << static_cast<std::uint16_t>(ref_meta.transfer_kind)  //
                  << " port=" << ref_meta.port_id                                    //
                  << " nid=" << static_cast<std::uint16_t>(ref_meta.remote_node_id)  //
                  << " tid=" << static_cast<std::uint16_t>(ref_meta.transfer_id)     //
                  << std::endl;
    }

    REQUIRE(0 == std::size(pending_transfers));
}
