<div align="center">

# Cyphal/CAN transport in C

[![CI](https://github.com/OpenCyphal/libcanard/actions/workflows/main.yml/badge.svg)](https://github.com/OpenCyphal/libcanard/actions/workflows/main.yml)
[![Forum](https://img.shields.io/discourse/users.svg?server=https%3A%2F%2Fforum.opencyphal.org&color=1700b3)](https://forum.opencyphal.org)

</div>

-----

Libcanard is a robust implementation of the Cyphal/CAN transport layer in C for high-integrity real-time embedded systems.
Supports Cyphal v1.1, v1.0, and legacy UAVCAN v0 aka DroneCAN.

The library supports both Classic CAN and CAN FD, in redundant and non-redundant configurations.

The library is compatible with 8/16/32/64-bit platforms, including extremely resource-constrained
baremetal MCU platforms starting from 32K ROM and 32K RAM.

The interface with the underlying platform is very clean and minimal, enabling quick adaptation to any CAN-enabled MCU.
The OpenCyphal Development Team maintains a collection of various platform-specific components in a separate repository
at <https://github.com/OpenCyphal/platform_specific_components>.
Users are encouraged to search through that repository for drivers, examples, and other pieces that may be
reused in the target application to speed up the design of the media IO layer (driver) for the application.

[Cyphal](https://opencyphal.org) is an open lightweight data bus standard designed for reliable intravehicular
communication in aerospace and robotic applications via CAN bus, Ethernet, and other robust transports.

**Read the docs in [`libcanard/canard.h`](libcanard/canard.h).**

## Revisions

To release a new version, simply publish a new release on GitHub.

### v5.0 [WORK IN PROGRESS]

v5 is a major rework based on the experience gained from extensive production use of the previous revisions.
The API has been redesigned from scratch and as such there is no migration guide available;
please read the new `canard.h` to see how to use the library -- the new API is simpler than the old one.

Main changes:

- Support for new protocols alongside Cyphal v1.0:
  - Cyphal/CAN v1.1, which adds support for 16-bit subject-IDs (like in UAVCAN v0) via a new CAN ID layout format.
  - UAVCAN v0 aka DroneCAN, a legacy predecessor to Cyphal v1.0 that is still widely used.

- Anonymous messages can no longer be transmitted, but they can still be received.

- A new passive node-ID autoconfiguration based on a simple occupancy observer.
  This method is decentralized and is compatible with old nodes.
  A node-ID can still be assigned manually if needed.

- Automatic CAN acceptance filter configuration based on the current subscription set.
  The configuration is refreshed whenever the subscription set is modified or the local node-ID is changed.

- New TX pipeline to use per-transfer queue granularity with efficient CAN frame deduplication across redundant
  interfaces, which resulted in a major reduction of the heap memory footprint in non-redundant interface
  configurations, and even more so in applications using redundant interfaces (typ. over 2x heap use reduction).

- New RX pipeline to support priority level preemption without transfer loss and reduce memory consumption.
  The old v4 revision was susceptible to transfer loss when the remote initiated a higher-priority multi-frame
  transfer while a lower-priority multi-frame transfer was in flight. The v5 revision maintains concurrent
  reassemblers per priority level, enabling arbitrary priority nesting.

### v4.0

Updating from Libcanard v3 to v4 involves several changes in memory management and TX frame expiration.
Please follow the `MIGRATION_v3.x_to_v4.0.md` guide available in the v4 codebase.

### v3.2

- Added new `canardRxGetSubscription`.

### v3.1

- Remove the Dockerfile; use [toolshed](https://github.com/OpenCyphal/docker_toolchains/pkgs/container/toolshed)
  instead if necessary.
- Simplify the transfer reassembly state machine to address [#212](https://github.com/OpenCyphal/libcanard/issues/212).
  See also <https://forum.opencyphal.org/t/amendment-to-the-transfer-reception-state-machine-implementations/1870>.

#### v3.1.1

- Refactor the transfer reassembly state machine to enhance its maintainability and robustness.

#### v3.1.2

- Allow redefinition of CANARD_ASSERT via the config header;
  see [#219](https://github.com/OpenCyphal/libcanard/pull/219).

### v3.0

- Update branding as [UAVCAN v1 is renamed to Cyphal](https://forum.opencyphal.org/t/uavcan-v1-is-now-cyphal/1622).
- Improve MISRA compliance by removing use of flex array: ([#192](https://github.com/OpenCyphal/libcanard/pull/192)).
- Fix dependency issues in docker toolchain.

  There are no API changes in this release aside from the rebranding/renaming:
  `CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR` -> `CANARD_CYPHAL_SPECIFICATION_VERSION_MAJOR`
  `CANARD_UAVCAN_SPECIFICATION_VERSION_MINOR` -> `CANARD_CYPHAL_SPECIFICATION_VERSION_MINOR`

#### v3.0.1

- Remove UB as described in [203](https://github.com/OpenCyphal/libcanard/issues/203).

#### v3.0.2

- Robustify the multi-frame transfer reassembler state machine
  ([#189](https://github.com/OpenCyphal/libcanard/issues/189)).
- Eliminate the risk of a header file name collision by renaming the vendored Cavl header to `_canard_cavl.h`
  ([#196](https://github.com/OpenCyphal/libcanard/issues/196)).

### v2.0

- Dedicated transmission queues per redundant CAN interface with depth limits.
  The application is now expected to instantiate `CanardTxQueue` (or several in case of redundant transport) manually.

- Replace O(n) linked lists with fast O(log n) AVL trees
  ([Cavl](https://github.com/pavel-kirienko/cavl) library is distributed with libcanard).
  Traversing the list of RX subscriptions now requires recursive traversal of the tree.

- Manual DSDL serialization helpers removed; use [Nunavut](https://github.com/OpenCyphal/nunavut) instead.

- Replace bitwise CRC computation with much faster static table by default
  ([#185](https://github.com/OpenCyphal/libcanard/issues/185)).
  This can be disabled by setting `CANARD_CRC_TABLE=0`, which is expected to save ca. 500 bytes of ROM.

- Fixed issues with const-correctness in the API ([#175](https://github.com/OpenCyphal/libcanard/issues/175)).

- `canardRxAccept2()` renamed to `canardRxAccept()`.

- Support build configuration headers via `CANARD_CONFIG_HEADER`.

- Add API for generating CAN hardware acceptance filter configurations
  ([#169](https://github.com/OpenCyphal/libcanard/issues/169)).

### v1.1

- Add new API function `canardRxAccept2()`, deprecate `canardRxAccept()`.
- Provide user references in `CanardRxSubscription`.
- Promote certain internal fields to the public API to allow introspection.

### v1.0

The initial release.
