/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
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

#include <map>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cannelloni {

/*
 * Template for a simple CSV Parser that
 * parses a file that contains key:value pairs
 * and returns it as a map
 */

template <class K, class V>
class CSVMapParser {
  public:
    bool open(const std::string &filename);
    bool parse();
    std::map<K,V>& read();
    bool close();
  private:
    std::map<K,V> m_map;
    std::ifstream m_fs;
};

template <class K, class V>
bool CSVMapParser<K,V>::open(const std::string &filename) {
  if (m_fs.is_open())
    return false;
  m_fs.open(filename.c_str(), std::ios::in);
  if (m_fs.fail())
    return false;
  else
    return true;
}

template <class K, class V>
bool CSVMapParser<K,V>::parse() {
  std::string line;
  std::string keystr, valuestr;
  K key;
  V value;
  std::size_t pos;
  m_map.clear();
  if (!m_fs.is_open())
    return false;
  while (getline(m_fs, line)) {
    /* if first character is a # */
    if (line.find_first_of('#') < line.find_last_not_of('#')) {
      continue;
    }
    /* Find delim */
    pos = line.find(',');
    if (pos == std::string::npos) {
      return false;
    }
    /* Extract key */
    keystr = line.substr(0, pos);
    valuestr = line.substr(pos+1);
    std::istringstream kss(keystr);
    kss >> key;
    std::istringstream vss(valuestr);
    vss >> value;

    if (kss.fail() || vss.fail()) {
      return false;
    }
    m_map.insert(std::pair<K,V>(key,value));
  }
  return true;
}

template <class K, class V>
bool CSVMapParser<K,V>::close() {
  if (!m_fs.is_open())
    return false;
  m_fs.close();
  return true;
}

template <class K, class V>
std::map<K,V>& CSVMapParser<K,V>::read() {
  return m_map;
}

};
