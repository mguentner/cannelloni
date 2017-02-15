#!/usr/bin/env python3
#
# This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
#
# Copyright (C) 2014-2017 Maximilian Güntner <code@sourcediver.org>

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#

import sys
import csv

# Usage:
#
# ./candump_compare.py candump.log
# candump.log is a log file produced by
#
# # candump -l
#
# The logfile must contain the logs of both interfaces.
# This can be achieved by starting candump like this,
#
# # candump -l vcan0 vcan1
#
# or by simply combining two files (cat file1.log file2.log > file12.log)
#
# Important:
#
# IDs must be unique, use e.q. cangen -I i can0 to create frames.
#
# Note that results may not be accurate when collecting on two different
# hosts. Use ntpdate prior to each measurement!
#

def parse_frame(frame):
    data = {}
    if frame.count('#') == 1:
        data['fd'] = False
    elif frame.count('#') == 2:
        data['fd'] = True
    else:
        raise ValueError("Frame not valid")

    frame_s = frame.split('#')
    data['id'] = int(frame_s[0],16)
    if data['fd'] == True:
        flags = int(frame_s[1][:2], 16)
        data['flags'] = flags
        data['data'] = frame_s[1][2:]
    else:
        data['data'] = frame_s[1]
    return data


def parse_candump(path):
    data = {}
    with open(path, newline='') as f:
        reader = csv.reader(f, delimiter=' ')
        for row in reader:
            arrival = {}
            frame = parse_frame(row[2])
            canId = frame['id']
            if not canId in data:
                data[canId] = []
            arrival['time'] = float(row[0][1:][:-1])
            arrival['bus'] = row[1]
            arrival['frame'] = frame
            data[canId].append(arrival)
    return data


def main():
    loss = 0
    dup  = 0
    count = 0
    correct  = 0
    corrupt = 0
    delay_total = 0
    delay_min = sys.maxsize
    delay_max = 0

    dump = parse_candump(sys.argv[1])
    for canId, arrivals in dump.items():
        if len(arrivals) == 1:
            loss += 1
        elif len(arrivals) > 2:
            dup  += 1
        else: # 2
            first = arrivals[0]
            second = arrivals[1]
            if first['frame']['data'] != second['frame']['data']:
                corrupt += 1
            else:
                if first['time'] > second['time']:
                    swap(first,second)
                delay = second['time']-first['time']
                delay_total += delay
                delay_min = min(delay_min, delay)
                delay_max = max(delay_max, delay)
                correct += 1
    print("Loss: {} Dups: {} Corrupt: {} Correct RX/TX: {}".format(loss, dup, corrupt, correct))
    print("Delay: {} min {} max {} avg [seconds]".format(delay_min, delay_max, delay_total/correct))

if __name__ == "__main__":
    main()
