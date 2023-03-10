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

#pragma once

#include <cstdint>
#include <linux/can.h>
#include <sys/types.h>

#define CAN_ID_SIZE_BYTES 4
#define CAN_LEN_SIZE_BYTES 1
#define CAN_FLAGS_SIZE_BYTES 1
#define MAX_TRANSMIT_BUFFER_SIZE_BYTES CAN_ID_SIZE_BYTES + CAN_LEN_SIZE_BYTES + CAN_FLAGS_SIZE_BYTES + CANFD_MAX_DLEN

enum DecodeState {
  STATE_INIT,
  STATE_CAN_ID,
  STATE_LEN,
  STATE_FLAGS,
  STATE_DATA,
};

struct Decoder {
  canfd_frame tempFrame;
  ssize_t expectedBytes;
  DecodeState state;

  Decoder() { reset(); }

  void reset() {
    expectedBytes = 0;
    state = STATE_INIT;
  }
};

/**
 * Decodes a CAN frame from input data.
 *
 * @param data Pointer to the input data to be decoded.
 * @param len The length of the input data in bytes.
 * @param frame Pointer to the CAN frame structure where the decoded frame will be stored.
 * @param state Pointer to a variable that tracks the state of the decoding process.
 * @return On success, the number of bytes remaining to be read after decoding the current segment of the frame. On error, a negative value indicating the error. If the decoding process completes successfully, this function will return 0. `frame` will then contain all decoded data.
 */
ssize_t decodeFrame(uint8_t *data, size_t len, canfd_frame *frame, DecodeState *state);
