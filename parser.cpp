#include "parser.h"

#include <arpa/inet.h>
#include <string.h>
#include <stdexcept>

void parseFrames(uint16_t len, const uint8_t* buffer, std::function<canfd_frame*()> frameAllocator,
        std::function<void(canfd_frame*, bool)> frameReceiver)
{
    using namespace cannelloni;

    const struct CannelloniDataPacket* data;
    /* Check for OP Code */
    data = reinterpret_cast<const struct CannelloniDataPacket*> (buffer);
    if (data->version != CANNELLONI_FRAME_VERSION)
        throw std::runtime_error("Received wrong version");

    if (data->op_code != DATA)
        throw std::runtime_error("Received wrong OP code");

    if (ntohs(data->count) == 0)
        return; // Empty packets silently ignored

    const uint8_t* rawData = buffer + CANNELLONI_DATA_PACKET_BASE_SIZE;

    for (uint16_t i = 0; i < ntohs(data->count); i++)
    {
        if (rawData - buffer + CANNELLONI_FRAME_BASE_SIZE > len)
            throw std::runtime_error("Received incomplete packet");

        /* We got at least a complete canfd_frame header */
        canfd_frame* frame = frameAllocator();
        if (!frame)
            throw std::runtime_error("Allocation error.");

        canid_t tmp;
        memcpy(&tmp, rawData, sizeof (canid_t));
        frame->can_id = ntohl(tmp);
        /* += 4 */
        rawData += sizeof (canid_t);
        frame->len = *rawData;
        /* += 1 */
        rawData += sizeof (frame->len);
        /* If this is a CAN FD frame, also retrieve the flags */
        if (frame->len & CANFD_FRAME)
        {
            frame->flags = *rawData;
            /* += 1 */
            rawData += sizeof (frame->flags);
        }
        /* RTR Frames have no data section although they have a dlc */
        if ((frame->can_id & CAN_RTR_FLAG) == 0)
        {
            /* Check again now that we know the dlc */
            if (rawData - buffer + canfd_len(frame) > len)
            {
                frame->len = 0;
                frameReceiver(frame, false);

                throw std::runtime_error("Received incomplete packet / can header corrupt!");
            }

            memcpy(frame->data, rawData, canfd_len(frame));
            rawData += canfd_len(frame);
        }

        frameReceiver(frame, true);
    }
}

uint8_t* buildPacket(uint16_t len, uint8_t* packetBuffer,
        std::list<canfd_frame*>& frames, uint8_t seqNo,
        std::function<void(std::list<canfd_frame*>&, std::list<canfd_frame*>::iterator)> handleOverflow)
{
    using namespace cannelloni;

    uint16_t frameCount = 0;
    uint8_t* data = packetBuffer + CANNELLONI_DATA_PACKET_BASE_SIZE;
    for (auto it = frames.begin(); it != frames.end(); it++)
    {
        canfd_frame* frame = *it;
        /* Check for packet overflow */
        if ((data - packetBuffer + CANNELLONI_FRAME_BASE_SIZE + canfd_len(frame)
                + ((frame->len & CANFD_FRAME) ? sizeof(frame->flags) : 0))
                > len)
        {
            handleOverflow(frames, it);
            break;
        }
        canid_t tmp = htonl(frame->can_id);
        memcpy(data, &tmp, sizeof(canid_t));
        /* += 4 */
        data += sizeof(canid_t);
        *data = frame->len;
        /* += 1 */
        data += sizeof(frame->len);
        /* If this is a CAN FD frame, also send the flags */
        if (frame->len & CANFD_FRAME)
        {
            *data = frame->flags;
            /* += 1 */
            data += sizeof(frame->flags);
        }
        if ((frame->can_id & CAN_RTR_FLAG) == 0)
        {
            memcpy(data, frame->data, canfd_len(frame));
            data += canfd_len(frame);
        }
        frameCount++;
    }
    struct CannelloniDataPacket* dataPacket;
    dataPacket = (struct CannelloniDataPacket*) (packetBuffer);
    dataPacket->version = CANNELLONI_FRAME_VERSION;
    dataPacket->op_code = DATA;
    dataPacket->seq_no = seqNo;
    dataPacket->count = htons(frameCount);

    return data;
}
