/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
 *
 * Copyright (C) 2014-2023 Maximilian Güntner <code@mguentner.de>
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

#include <cstring>
#include <netinet/in.h>
#include "decoder.h"
#include "cannelloni.h"

#define CAN_ID_SIZE_BYTES 4
#define CAN_LEN_SIZE_BYTES 1
#define CAN_FLAGS_SIZE_BYTES 1
#define MAX_TRANSMIT_BUFFER_SIZE_BYTES CAN_ID_SIZE_BYTES + CAN_LEN_SIZE_BYTES + CAN_FLAGS_SIZE_BYTES + CANFD_MAX_DLEN

using namespace cannelloni;

ssize_t decodeFrame(uint8_t *data, size_t len, canfd_frame *frame, DecodeState *state) {
  switch (*state) {
  case STATE_INIT:
    *state = STATE_CAN_ID;
    return CAN_ID_SIZE_BYTES;
  case STATE_CAN_ID:
    if (len != CAN_ID_SIZE_BYTES) {
      return -1;
    }
    canid_t tmp;
    memcpy(&tmp, data, sizeof(canid_t));
    frame->can_id = ntohl(tmp);
    *state = STATE_LEN;
    return CAN_LEN_SIZE_BYTES;
  case STATE_LEN:
    if (len != CAN_LEN_SIZE_BYTES) {
      return -1;
    }
    frame->len = *data;
    /* If this is a CAN FD frame, also retrieve the flags */
    if (frame->len & CANFD_FRAME) {
      *state = STATE_FLAGS;
      return CAN_FLAGS_SIZE_BYTES;
    }
    /* RTR Frames have no data section although they have a dlc */
    if (frame->can_id & CAN_RTR_FLAG) {
      *state = STATE_INIT;
      frame->len=0;
      return 0;
    }
    if (canfd_len(frame) == 0) {
      *state = STATE_INIT;
      return 0;
    }
    *state = STATE_DATA;
    return canfd_len(frame);
  case STATE_FLAGS:
    if (len != CAN_FLAGS_SIZE_BYTES) {
      return -1;
    }
    frame->flags = *data;
    /* RTR Frames have no data section although they have a dlc */
    if (frame->can_id & CAN_RTR_FLAG) {
      *state = STATE_INIT;
      return 0;
    }
    if (canfd_len(frame) == 0) {
      *state = STATE_INIT;
      return 0;
    }
    *state = STATE_DATA;
    return canfd_len(frame);
  case STATE_DATA:
    if (len != canfd_len(frame)) {
      return -1;
    }
    memcpy(frame->data, data, canfd_len(frame));
    *state = STATE_INIT;
    return 0;
  }
  return -1;
}
