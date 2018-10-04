# DSDL compiler for libcanard

## Overview

Libcanard_dsdlc is a tool for converting UAVCAN DSDL definitions into libcanard-compatible C source files or headers.

Modules have: defines, enums, unions, structs, and encoding/decoding functions as defined in UAVCAN DSDL.
Encoding and decoding functions use the encode and decode functions of libcanard for bit packing/unpacking.

In C there is no namespace, so all the generated `#define` and function names are long having full folder path included.
This is made to prevent collisions with each other and with the rest of the system.

## Installation & integration

To get libcanard from git, make sure all the submodules are fetched too.

```
git submodules update --init --recursive
```

## Compilation

### When using c-modules

```
python3 libcanard_dsdlc --outdir <outdir> <dsdl-definition-uavcan-folder>
```

Add all or only selected message C-files to your build script (e.g. Makefile).
Add `<dsdl-generate-output-folder>` to your include paths.

### When using as header only library

```
python3 libcanard_dsdlc --header_only --outdir <outdir> <dsdl-definition-uavcan-folder>
```

Include wanted message header(s) into your code.
Add `<dsdl-generate-output-folder>` to your include paths.

### Notes

#### Float16

Generated structs use the native `float` type for `float16`.
The native `float` is converted to `float16` using libcanard's `canardConvertNativeFloatToFloat16()` when encoding.
Calling decode function after reception will convert `float16` to the native `float` using the libcanard's
`canardConvertFloat16ToNativeFloat()` function.
Libcanard conversion functions can be replaced to compiler casting if wanted,
e.g. `#define CANARD_USE_FLOAT16_CAST __fp16`.

## Using generated modules

### Encode NodeStatus message

```cpp
#include "uavcan/protocol/NodeStatus.h"

/* Reserve memory and struct for messages */
uint8_t packed_uavcan_msg_buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
/* MAX_SIZE comes from module header as pre-calculated */
uavcan_protocol_NodeStatus msg;

msg.uptime_sec = getUptime();
msg.health = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
msg.mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
msg.sub_mode = sub_mode;
msg.vendor_specific_status_code = vendor_status_code;

/* Encode the filled struct into packed_uavcan_msg_buf, ready to be sent */
const uint32_t len_of_packed_msg = uavcan_protocol_NodeStatus_encode(&msg, packed_uavcan_msg_buf);

(void) canardBroadcast(&g_canard,
                       UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                       UAVCAN_PROTOCOL_NODESTATUS_ID,
                       &g_bc_node_status_transfer_id,
                       CANARD_TRANSFER_PRIORITY_MEDIUM,
                       packed_uavcan_msg_buf,
                       len_of_packed_msg);
```

Dynamic arrays also have the `_len` field,
which specifies how many data items are accessible via the dynamic array pointer.

#### Decode GetSet request

```cpp
/* include header */
#include "uavcan/protocol/param/GetSet.h"

#define GETSETREQ_NAME_MAX_SIZE 96 // max size needed for the dynamic arrays
/* Reserve some memory for the dynamic arrays from the stack */
uint8_t buff[GETSETREQ_NAME_MAX_SIZE];
uint8_t* dyn_buf_ptr = buff;

/* Reserve struct */
uavcan_protocol_param_GetSetRequest get_set_req;

/* NOTE get_set_req struct will be cleared in the Decode function first */
(void) uavcan_protocol_param_GetSetRequest_decode(transfer,
                                                  (uint16_t)transfer->payload_len,
                                                  &get_set_req,
                                                  &dyn_buf_ptr);

/* Now the struct get_set_req "object" is ready to be used */
```

`dyn_buf_ptr` is a way to give allocated memory to the Decode function,
to use that space to store dynamic arrays into it, and store the pointer to struct pointer.

NOTE: There is no check whether dynamic memory allocation is sufficient.

## License

Released under the MIT license, check the file LICENSE.
