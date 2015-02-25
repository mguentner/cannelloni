#cannelloni UDP/SCTP Format version 2

##Data Frames

Each data frame can contain several CAN frames.

The header of a data frame contains the following
information:


| Bytes |  Name   |   Description       |
|-------|---------|---------------------|
|   1   | Version | Protocol Version    |
|   1   | OP Code | Type of Frame (DATA)|
|   1   | Seq No  | Sequence number     |
|   2   | Count   | Number of CAN Frames|

After the header, the data section follows.
Each CAN frame has the following format which
is similar to `canfd_frame` defined in `<linux/can.h>`.

| Bytes |  Name   |   Description       |
|-------|---------|---------------------|
|   4   |  can_id |  see `<linux/can.h>`|
|   1   |  len    |  size of payload/dlc|
|   1   |  flags^ |  CAN FD flags       |
|0-8/64 |  data   |  Data section       |

^ = CAN FD only

Everything is Big-Endian/Network Byte Order.

*The frame format is identical for UDP and SCTP*

##CAN FD

CAN FD frames are marked with the MSB of `len` being
set, so `len | (0x80)`. If this bit is set, the `flags`
attribute is inserted between `len` and `data`.
For CAN 2.0 frames this attribute is missing.
`data` can be 0-8 Bytes long for CAN 2.0 and 0-64 Bytes
for CAN FD frames.
