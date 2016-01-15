#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <pthread.h>
#include "canard.h"
// #include "canard_debug.h"
/*
 * This part is specific for Linux. It can be replaced for use on other platforms.
 * Note that under Linux this source must be linked with librt (for GCC add -lrt)
 */
#include <sys/time.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <time.h>

#include <inttypes.h>
#include <pthread.h>

#define CLEANUP_STALE_TRANSFERS 2000000

#define TIME_TO_SEND_NODE_STATUS 101000000
#define TIME_TO_SEND_AIRSPEED 51000000000
#define TIME_TO_SEND_MULTI 1000000
#define TIME_TO_SEND_REQUEST 1000000000
static int can_socket = -1;

int can_init(const char* can_iface_name)
{
    // Open the SocketCAN socket
    const int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0)
    {
        return -1;
    }

    // Resolve the iface index
    struct ifreq ifr;
    (void)memset(&ifr, 0, sizeof(ifr));
    (void)strncpy(ifr.ifr_name, can_iface_name, IFNAMSIZ);
    const int ioctl_result = ioctl(sock, SIOCGIFINDEX, &ifr);
    if (ioctl_result < 0)
    {
        return -1;
    }

    // Assign the iface
    struct sockaddr_can addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    // Bind the socket to the iface
    const int bind_result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (bind_result < 0)
    {
        return -1;
    }

    can_socket = sock;
    return 0;
}
static uint8_t uavcan_node_id;

int can_send(uint32_t extended_can_id, const uint8_t* frame_data, uint8_t frame_data_len)
{
    if (frame_data_len > 8 || frame_data == NULL)
    {
        return -1;
    }

    struct can_frame frame;
    frame.can_id  = extended_can_id | CAN_EFF_FLAG;
    frame.can_dlc = frame_data_len;
    memcpy(frame.data, frame_data, frame_data_len);

    return write(can_socket, &frame, sizeof(struct can_frame));
}

uint64_t get_monotonic_usec(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000UL;
}

/// Arbitrary priority values
static const uint8_t PRIORITY_HIGHEST = 0;
static const uint8_t PRIORITY_HIGH    = 8;
static const uint8_t PRIORITY_MEDIUM  = 16;
static const uint8_t PRIORITY_LOW     = 24;
static const uint8_t PRIORITY_LOWEST  = 31;

/// Defined for the standard data type uavcan.protocol.NodeStatus
enum node_health
{
    HEALTH_OK       = 0,
    HEALTH_WARNING  = 1,
    HEALTH_ERROR    = 2,
    HEALTH_CRITICAL = 3
};

/// Defined for the standard data type uavcan.protocol.NodeStatus
enum node_mode
{
    MODE_OPERATIONAL     = 0,
    MODE_INITIALIZATION  = 1,
    MODE_MAINTENANCE     = 2,
    MODE_SOFTWARE_UPDATE = 3,
    MODE_OFFLINE         = 7
};
/// Standard data type: uavcan.protocol.NodeStatus
int publish_node_status(CanardInstance* ins, enum node_health health, enum node_mode mode, uint16_t vendor_specific_status_code)
{
    static uint64_t startup_timestamp_usec;
    if (startup_timestamp_usec == 0)
    {
        startup_timestamp_usec = get_monotonic_usec();
    }

    uint8_t payload[7];

    // Uptime in seconds
    const uint32_t uptime_sec = (get_monotonic_usec() - startup_timestamp_usec) / 1000000ULL;
    payload[0] = (uptime_sec >> 0)  & 0xFF;
    payload[1] = (uptime_sec >> 8)  & 0xFF;
    payload[2] = (uptime_sec >> 16) & 0xFF;
    payload[3] = (uptime_sec >> 24) & 0xFF;

    // Health and mode
    payload[4] = ((uint8_t)health << 6) | ((uint8_t)mode << 3);

    // Vendor-specific status code
    payload[5] = (vendor_specific_status_code >> 0) & 0xFF;
    payload[6] = (vendor_specific_status_code >> 8) & 0xFF;

    static const uint16_t data_type_id = 341;
    static uint8_t transfer_id;
    uint64_t data_type_signature = 0x8899AABBCCDDEEFF;
    return canardBroadcast(ins, data_type_signature, 
        data_type_id, &transfer_id, PRIORITY_LOW, payload, sizeof(payload));
}
/*
 * Float16 support
 */
uint16_t make_float16(float value)
{
    union fp32
    {
        uint32_t u;
        float f;
    };

    const union fp32 f32infty = { 255U << 23 };
    const union fp32 f16infty = { 31U << 23 };
    const union fp32 magic = { 15U << 23 };
    const uint32_t sign_mask = 0x80000000U;
    const uint32_t round_mask = ~0xFFFU;

    union fp32 in;
    uint16_t out = 0;

    in.f = value;

    uint32_t sign = in.u & sign_mask;
    in.u ^= sign;

    if (in.u >= f32infty.u)
    {
        out = (in.u > f32infty.u) ? 0x7FFFU : 0x7C00U;
    }
    else
    {
        in.u &= round_mask;
        in.f *= magic.f;
        in.u -= round_mask;
        if (in.u > f16infty.u)
        {
            in.u = f16infty.u;
        }
        out = (uint16_t)(in.u >> 13);
    }

    out |= (uint16_t)(sign >> 16);

    return out;
}
/// Standard data type: uavcan.equipment.air_data.TrueAirspeed
int publish_true_airspeed(CanardInstance* ins, float mean, float variance)
{
    const uint16_t f16_mean     = make_float16(mean);
    const uint16_t f16_variance = make_float16(variance);

    uint8_t payload[4];
    payload[0] = (f16_mean >> 0) & 0xFF;
    payload[1] = (f16_mean >> 8) & 0xFF;
    payload[2] = (f16_variance >> 0) & 0xFF;
    payload[3] = (f16_variance >> 8) & 0xFF;

    static const uint16_t data_type_id = 1020;
    static uint8_t transfer_id;
    uint64_t data_type_signature = 0x8899AABBCCDDEEFF;
    return canardBroadcast(ins, data_type_signature, 
        data_type_id, &transfer_id, PRIORITY_MEDIUM, payload, sizeof(payload));
}

/// Standard data type: uavcan.equipment.multi
int publish_multi(CanardInstance* ins)
{
    static int len = 80;
    uint8_t payload[len];
    uint8_t i;
    for (i=0; i<len; i++)
    {
        payload[i] = i+1;
    }
    static const uint16_t data_type_id = 420;
    static uint8_t transfer_id;
    uint64_t data_type_signature = 0x8899AABBCCDDEEFF;
    return canardBroadcast(ins, data_type_signature, 
        data_type_id, &transfer_id, PRIORITY_HIGH, payload, sizeof(payload));
}

int publish_request(CanardInstance* ins)
{
    static int len = 43;
    uint8_t payload[len];
    uint8_t i;
    for (i=0; i<len; i++)
    {
        payload[i] = i+3;
    }
    uint8_t dest_id = 33;
    static const uint16_t data_type_id = 15;
    static uint8_t transfer_id;
    uint64_t data_type_signature = 0x8899AABBCCDDEEFF;
    return canardRequestOrRespond(ins, dest_id, data_type_signature, 
        data_type_id, &transfer_id, PRIORITY_LOW, CanardRequest, payload, sizeof(payload));
}

int compute_true_airspeed(float* out_airspeed, float* out_variance)
{
    *out_airspeed = 1.2345F; // This is a stub.
    *out_variance = 0.0F;    // By convention, zero represents unknown error variance.
    return 0;
}

// Standard data type: uavcan.equipment.

void printframe(const CanardCANFrame* frame)
{
    printf("%X ",frame->id);
    printf("[%u] ",frame->data_len);
    int i;
    for(i = 0; i < frame->data_len; i++)
    {
        printf(" %u ", frame->data[i]);
    }
    printf("\n");
}

void on_reception(CanardInstance* ins, CanardRxTransfer* transfer)
{
    // printf("\n");
    printf("transfer type: ");
    switch(transfer->transfer_type) {
        case CanardTransferTypeResponse:
            printf("reponse\n");
        case CanardTransferTypeRequest:
            printf("request\n");
        case CanardTransferTypeBroadcast:
            printf("broadcast\n");
    }
    unsigned char payload[transfer->payload_len];
    if (transfer->payload_len > 7)
    {
    CanardBufferBlock* block = transfer->payload_middle;
    // printf("sizeof payload head: %lu\n",sizeof(transfer->payload_head));
    // printf("sizeof payload tail: %lu\n",sizeof(transfer->payload_tail));
    int i, index = 0;

    if(CANARD_RX_PAYLOAD_HEAD_SIZE > 0)
    {
        for (i=0; i < CANARD_RX_PAYLOAD_HEAD_SIZE; i++)
        {
            payload[i] = transfer->payload_head[i];
        }
        index = i;
    }

    for (i=0; index < (CANARD_RX_PAYLOAD_HEAD_SIZE + transfer->middle_len); i++, index++)
    {
        payload[index] = block->data[i];
        if(i==CANARD_BUFFER_BLOCK_DATA_SIZE-1)
        {
            i = -1;
            block = block->next;
        }
    }

    //printf("tail:\n");
    int tail_len = transfer->payload_len - (CANARD_RX_PAYLOAD_HEAD_SIZE + transfer->middle_len);
    for (i=0; i<(tail_len);i++, index++)
    {
        payload[index] = transfer->payload_tail[i];
    }
    
    }
    else
    {
        uint8_t i;
        for (i = 0; i<transfer->payload_len; i++)
        {
            payload[i] = transfer->payload_head[i];
        }
    }
    canardReleaseRxTransferPayload(ins, transfer);
    //do stuff with the data then call canardReleaseRxTransferPayload() if there are blocks (multi-frame transfers)

    int i;
    for (i=0;i<sizeof(payload);i++) {
        printf("%02X ", payload[i]);
    }
    printf("\n");
}

bool should_accept(const CanardInstance* ins, uint64_t* out_data_type_signature,
                    uint16_t data_type_id, CanardTransferType transfer_type, uint8_t source_node_id)
{   
    *out_data_type_signature = 0x8899AABBCCDDEEFF;
    return true;
}

// returns true with a probability of probability 
bool random_drop(double probability)
{
    return rand() <  probability * ((double)RAND_MAX + 1.0);
}

void *receiveThread(void* canard_instance)
{
    struct can_frame r_frame;
    CanardCANFrame canard_frame;
    // CanardCANFrame* transmit_frame;
    int nbytes = 0;
    while(1)
    {
        nbytes = read(can_socket, &r_frame, sizeof(struct can_frame));
        if (nbytes < 0)
        {
            perror("can raw socket read");
            return NULL;
        }
        else if (nbytes == 0)
        {
            printf("problem");
        }
        canard_frame.id = r_frame.can_id;
        //printf("%X\n",CANARD_MSG_TYPE_FROM_ID(r_frame.can_id));
        canard_frame.data_len = r_frame.can_dlc;
        memcpy(canard_frame.data, &r_frame.data, r_frame.can_dlc);
        //printframe(&canard_frame);
        canardHandleRxFrame(canard_instance, &canard_frame, get_monotonic_usec());
    }
}

void *sendThread(void* canard_instance) {
    enum node_health health = HEALTH_OK;
    uint64_t last_node_status = 0;
    uint64_t last_multi = 0;
    uint64_t last_clean = 0;
    uint64_t last_airspeed = 0;
    uint64_t last_request = 0;
    bool drop = false;
    while(1)
    {
        if ((get_monotonic_usec() - last_node_status) > TIME_TO_SEND_NODE_STATUS)
        {
            const uint16_t vendor_specific_status_code = rand(); // Can be used to report vendor-specific status info
            publish_node_status(canard_instance, health, MODE_OPERATIONAL, vendor_specific_status_code);
            last_node_status = get_monotonic_usec();
        }

        if ((get_monotonic_usec() - last_airspeed) > TIME_TO_SEND_AIRSPEED)
        {
            float airspeed = 0.0F;
            float airspeed_variance = 0.0F;
            const int airspeed_computation_result = compute_true_airspeed(&airspeed, &airspeed_variance);

            if (airspeed_computation_result == 0)
            {
                const int publication_result = publish_true_airspeed(canard_instance, airspeed, airspeed_variance);
                health = (publication_result < 0) ? HEALTH_ERROR : HEALTH_OK;
            }
            else
            {
                health = HEALTH_ERROR;
            }
            last_airspeed = get_monotonic_usec();
        }
        if ((get_monotonic_usec() - last_multi) > TIME_TO_SEND_MULTI)
        {
            publish_multi(canard_instance);
            last_multi = get_monotonic_usec();
        }
        if ((get_monotonic_usec() - last_request) > TIME_TO_SEND_REQUEST) {
            publish_request(canard_instance);
            last_request = get_monotonic_usec();
        }
        if ((get_monotonic_usec() - last_clean) > CLEANUP_STALE_TRANSFERS)
        {
            canardCleanupStaleTransfers(canard_instance, get_monotonic_usec());
            last_clean = get_monotonic_usec();
        }
        const CanardCANFrame *transmit_frame = canardPeekTxQueue(canard_instance);
        if (transmit_frame != NULL)
        {
            if (drop)
            {
                printf("dropping\n");
                // printframe(transmit_frame);
                //can_send(transmit_frame->id, transmit_frame->data, transmit_frame->data_len);
            }
            else
            {
                //printf("keeping\n");
                //printframe(transmit_frame);
                can_send(transmit_frame->id, transmit_frame->data, transmit_frame->data_len);
            }
            canardPopTxQueue(canard_instance);
            drop = random_drop(.0);
        }
    }
}

int main(int argc, char** argv)
{
    srand(time(NULL));
    /*
     * Node initialization
     */
    if (argc < 3)
    {
        puts("Args: <self-node-id> <can-iface-name>");
        return 1;
    }

    uavcan_node_id = (uint8_t)atoi(argv[1]);
    if (uavcan_node_id < 1 || uavcan_node_id > 127)
    {
        printf("%i is not a valid node ID\n", (int)uavcan_node_id);
        return 1;
    }

    if (can_init(argv[2]) < 0)
    {
        printf("Failed to open iface %s\n", argv[2]);
        return 1;
    }

    // /*
    //  * Main loop
    //  */
    // enum node_health health = HEALTH_OK;
    static CanardInstance canard_instance;
        
    canardInit(&canard_instance, on_reception, should_accept);
    canardSetLocalNodeID(&canard_instance,uavcan_node_id);
    printf("Initialized.\n");

    /*
     * Main loop
     */

    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, receiveThread, &canard_instance);
    pthread_create(&tid2, NULL, sendThread, &canard_instance);
    pthread_join(tid1, NULL);
    pthread_join(tid2,NULL);
}