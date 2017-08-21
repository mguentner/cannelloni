#ifndef PARSER_H_
#define PARSER_H_

#include "cannelloni.h"

#include <linux/can.h>

#include <functional>
#include <list>

/**
 * Parses Cannelloni packet and extracts CAN frames
 * If frameAllocator allocates heap memory or reserves resources in some preallocated buffer
 * you need to remember to free those resources up in the frameReceiver implementation.
 * parseFrames function does not claim ownership of resources allocated by frameAllocator.
 * In the case when incomplete packet is received frameReceiver will be passed a frame
 * with len set to 0, so that proper deallocation of resources can be done there.
 *
 * @param len Buffer length
 * @param buffer Pointer to buffer containing Cannelloni packet
 * @param frameAllocator Callback responsible for providing memory for CAN frame. Returns pointer
 *  to allocated frame.
 * @param frameReceiver Callback responsible for handling newly read CAN frame. First argument
 *  is pointer to frame allocated by frameAllocator and filled with data by parseFrames.
 *  Second argument tells whether frame was read with success and is correct (can be processed
 *  further), or whether there was some error and it should not be processed (apart from
 *  being deallocated)
 */
void parseFrames(uint16_t len, const uint8_t* buffer,
        std::function<canfd_frame*()> frameAllocator,
        std::function<void(canfd_frame*, bool)> frameReceiver);

/**
 * Builds Cannelloni packet from provided list of CAN frames
 * @param len Buffer length
 * @param packetBuffer Pointer to buffer that will contain Cannelloni packet
 * @param frames Reference to list of pointers to CAN frames
 * @param seqNo Packet sequence number
 * @param handleOverflow Callback responsible for handling CAN frames that did't fit
 *  into Cannelloni package. First argument is a frames list reference, second argument
 *  is iterator to the first not handled frame.
 * @return
 */
uint8_t* buildPacket(uint16_t len, uint8_t* packetBuffer,
        std::list<canfd_frame*>& frames, uint8_t seqNo,
        std::function<void(std::list<canfd_frame*>&, std::list<canfd_frame*>::iterator)> handleOverflow);

#endif /* PARSER_H_ */
