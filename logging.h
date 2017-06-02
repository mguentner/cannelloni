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
#include <iostream>
#include <iomanip>
#include <string>

#include "cannelloni.h"

using namespace cannelloni;

inline std::string splitFilename(const std::string &path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos)
    return path;
  else
    return path.substr(pos+1);
}

#define FUNCTION_STRING splitFilename(__FILE__) << "[" << std::dec <<  __LINE__ << "]:" << __FUNCTION__ << ":"
#define INFO_STRING "INFO:"
#define ERROR_STRING "ERROR:"
#define WARNING_STRING "WARNING:"

#define linfo std::cout << INFO_STRING << FUNCTION_STRING
#define lwarn std::cerr << WARNING_STRING << FUNCTION_STRING
#define lerror std::cerr << ERROR_STRING << FUNCTION_STRING

inline void printCANInfo(const canfd_frame *frame) {
  if (frame->len & CANFD_FRAME) {
    std::cout << "FD|";
  } else {
    std::cout << "LC|";
  }
  if (frame->can_id & CAN_EFF_FLAG) {
    std::cout << "EFF Frame ID[" << std::setw(5) << std::dec << (frame->can_id & CAN_EFF_MASK) << "]";
  } else {
    std::cout << "SFF Frame ID[" << std::setw(5) << std::dec << (frame->can_id & CAN_SFF_MASK) << "]";
  }
  if (frame->can_id & CAN_ERR_FLAG)
    std::cout << "\t ERROR\t";
  else
    std::cout << "\t Length:" << std::dec << (int) canfd_len(frame) << "\t";

  if (frame->can_id & CAN_RTR_FLAG)  {
      std::cout << "\tREMOTE";
  } else {
    /* This will also contain the error information */
    for (uint8_t i=0; i < canfd_len(frame); i++)
      std::cout << std::setbase(16) << " " << int(frame->data[i]);
  }
  std::cout << std::endl;
};
