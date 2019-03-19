#include "canard.h"
#include "canard_internals.h"
#include "canard_can.h"

int16_t canardCANPeekTxQueue(const CanardInstance* ins, CanardCANFrame** frame)
{
    if (ins->transport_protocol != CanardTransportProtocolCAN)
    {
        return -CANARD_ERROR_INCOMPATIBLE_TRANSPORT_PROTOCOL;
    }

    return canardPeekTxQueue(ins, (CanardTransportFrame**)frame);
}

int16_t canardCANHandleRxFrame(CanardInstance* ins, const CanardCANFrame* frame, uint64_t timestamp_usec)
{
    if (ins->transport_protocol != CanardTransportProtocolCAN)
    {
        return -CANARD_ERROR_INCOMPATIBLE_TRANSPORT_PROTOCOL;
    }

    return canardHandleRxFrame(ins, (CanardTransportFrame*)frame, timestamp_usec);
}

