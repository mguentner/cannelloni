/*
 * This file is part of cannelloni, a SocketCAN over ethernet tunnel.
 *
 * Copyright (C) 2014 Maximilian Güntner <maximilian.guentner@gmail.com>
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

#define FUNCTION_STRING __FILE__ << "[" <<  __LINE__ << "]:" << __FUNCTION__ << ":"
#define INFO_STRING "INFO:"
#define ERROR_STRING "ERROR:"
#define WARNING_STRING "WARNING:"

#define linfo std::cout << INFO_STRING << FUNCTION_STRING
#define lwarn std::cerr << WARNING_STRING << FUNCTION_STRING
#define lerror std::cerr << ERROR_STRING << FUNCTION_STRING
