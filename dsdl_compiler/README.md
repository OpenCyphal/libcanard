# UAVCAN DSDL compiler for the libcanard

## Overview
Libcanard_dsdlc is a tool to generate UAVCAN DSDL definitions to libcanard compatible message c-modules.

Modules have: defines, enums, unions, structs, and encoding and decoding functions as defined in UAVCAN DSDL. Encoding and decoding functions use the decode and encode functions of libcanard for bit packing / unpacking.

In c there is no namespace, so all the generated #define and function names are long having full folder path included. This is made to prevent the collision with each other and to the rest of the system.

## Install & Integration
To get libcanard from the git, make sure all the submodules are fetched too.

`git submodules update --init --recursive`

### Generating files
`python3 libcanard_dsdlc --outdir <outdir> <dsdl-definition-uavcan-folder>`

NOTE: If python2 is used, monotonic library is needed.

`pip install monotonic`

### Using generated c-modules
Include all or only selected message c-files to your compiler script (e.g. Makefile). Add include path to root of the <dsdl-generate-output-folder>.

NOTE: compiled *.o files can't be compiled into to flat "build" directory as some files in DSDL have the same name.

### Float16
Generated structs in modules use canard_float16 type when specified float16 in DSDL. Canard_float16 has to be defined as CANARD_FLOAT16 to something e.g. __fp16.

e.g. in Makefile

`
CFLAGS += -DCANARD_FLOAT16=__fp16
`

## Using generated modules

#### Encode NodeStatus-broadcast message
```cpp
 #include "uavcan/protocol/NodeStatus.h"

    /* Reserve memory and struct for messages */
    uint8_t packed_uavcan_msg_buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    /* MAX_SIZE comes from module header as pre-calculated */
    uavcan_protocol_NodeStatus msg;

    msg.uptime_sec = GetUptime();

    msg.health = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    msg.mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;

    msg.sub_mode = sub_mode;
    msg.vendor_specific_status_code = vendor_status_code;

    /* Encode filled struct to packed_uavcan_msg_buf, ready to be send */
    uint32_t len_of_packed_msg = uavcan_protocol_NodeStatusEncode(&msg, packed_uavcan_msg_buf);

    canardBroadcast(&g_canard,
                    UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                    UAVCAN_PROTOCOL_NODESTATUS_ID,
                    &g_bc_node_status_transfer_id,
                    CANARD_TRANSFER_PRIORITY_MEDIUM,
                    packed_uavcan_msg_buf,
                    len_of_packed_msg);
```

*Dynamic Array* all the dynamic arrays have also _len field, which contain the info of how many data items have been stored in to dynamic array pointer.

#### Decode GetSet-request

```cpp
    /* include header */
    #include "uavcan/protocol/param/GetSet.h"

    #define GETSETREQ_NAME_MAX_SIZE 96 // max size needed for the dynamic arrays
    /* Reserve some memory for the dynamic arrays from the stack */
    uint8_t buff[GETSETREQ_NAME_MAX_SIZE]; 
    uint8_t *dyn_buf_ptr = buff;

    /* Reserve struct */
    uavcan_protocol_param_GetSetRequest get_set_req;

    /* NOTE get_set_req struct will be cleared in Decode function first */
    uavcan_protocol_param_GetSetRequestDecode(transfer,
                                              (uint16_t)transfer->payload_len,
                                              &get_set_req,
                                              &dyn_buf_ptr);
    
    /* Now struct get_set_req "object" is ready to be used */
```

*Dynamic Arrays* dyn_buf_ptr is a way to give allocated memory to *Decode function, to use that space to store dynamic arrays into it, and store the pointer to struct pointer. 

NOTE: There is no check whether dynamic memory allocation is big enough.

## License

Released under MIT license, check LICENSE




