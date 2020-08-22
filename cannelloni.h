/*
 * This file is part of cannelloni, a SocketCAN over ethernet tunnel.
 *
 * Copyright (C) 2014-2017 Maximilian Güntner <code@sourcediver.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#pragma once

#include <stdint.h>
#include <linux/can.h>

namespace cannelloni {

/* Base size of a canfd_frame (canid + dlc) */
#define CANNELLONI_FRAME_BASE_SIZE 5
/* Size in byte of UDPDataPacket */
#define CANNELLONI_DATA_PACKET_BASE_SIZE 5

#define CANNELLONI_FRAME_VERSION 2
#define CANFD_FRAME              0x80

enum op_codes {DATA, ACK, NACK};

struct __attribute__((__packed__)) CannelloniDataPacket {
  /* Version */
  uint8_t version;
  /* OP Code */
  uint8_t op_code;
  /* Sequence Number */
  uint8_t seq_no;
  /* Number of CAN Messages in this packet */
  uint16_t count;
};

/*
 * Since we are buffering CAN Frames, it is a good idea
 * to order them by their identifier to mimic a CAN bus
 */
struct canfd_frame_comp
{
  inline bool operator() (const struct canfd_frame *f1,
                          const struct canfd_frame *f2) const
  {
    canid_t id1, id2;
    /* Be extra careful when doing the comparison */
    if (f1->can_id & CAN_EFF_FLAG)
      id1 = f1->can_id & CAN_EFF_MASK;
    else
      id1 = f1->can_id & CAN_SFF_MASK;

    if (f2->can_id & CAN_EFF_FLAG)
      id2 = f2->can_id & CAN_EFF_MASK;
    else
      id2 = f2->can_id & CAN_SFF_MASK;
    return id1 < id2;
  }
};

/* Helper function to get the real length of a frame */
inline uint8_t canfd_len(const struct canfd_frame *f) {
  return f->len & ~(CANFD_FRAME);
}

}
