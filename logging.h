/*
 * This file is part of cannelloni, a SocketCAN over ethernet tunnel.
 *
 * Copyright (C) 2014-2015 Maximilian Güntner <maximilian.guentner@gmail.com>
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
#include <string>

inline std::string splitFilename(const std::string &path) {
  uint16_t pos = path.find_last_of("/\\");
  return path.substr(pos+1);
}

#define FUNCTION_STRING splitFilename(__FILE__) << "[" << std::dec <<  __LINE__ << "]:" << __FUNCTION__ << ":"
#define INFO_STRING "INFO:"
#define ERROR_STRING "ERROR:"
#define WARNING_STRING "WARNING:"

#define linfo std::cout << INFO_STRING << FUNCTION_STRING
#define lwarn std::cerr << WARNING_STRING << FUNCTION_STRING
#define lerror std::cerr << ERROR_STRING << FUNCTION_STRING
