#cannelloni
*a SocketCAN over Ethernet tunnel*

cannelloni is written in C++11 and uses UDP to transfer CAN frames
between two machines.

Features:

- frame aggregation in UDP frames (multiple CAN frames in one UDP
  frame)
- efficient protocol
- very high data rates possible (40 Mbit/s +)
- custom timeouts for certain IDs (see below)
- easy debugging

Not yet supported (send a PR ;) ):

- CAN_FD
- Resend lost UDP frames
- Packet loss detection

##Compilation

cannelloni uses cmake to generate a Makefile.
You can build cannelloni using the following command.

```
cmake -DCMAKE_BUILD_TYPE=Release
make
```

##Installation

Just install it using

```
  make install
```

##Usage

###Example

Two machines 1 and 2 need to be connected:

![](doc/firstexp.png)

Machine 2 needs to be connected to the physical CAN Bus that is attached
to Machine 1.

Start cannelloni on Machine 1:

```
cannelloni -I slcan0 -R 192.168.0.3 -r 20000 -l 20000
```
cannelloni will now listen on port 20000 and has Machine 2 configured as
its remote.

Prepare vcan on Machine 2:

```
sudo modprobe vcan
sudo ip link add name vcan0 type vcan
sudo ip link set dev vcan0 up
```

When operating with `vcan` interfaces always keep in mind that they
easily surpass the possible data rate of any physical CAN interface.
An application that just sends whenever the bus is ready would simply
send with many Mbit/s.
The receiving end, a physical CAN interface with a net. data rate of
<= 1 Mbit/s would not be able to keep up.
It is therefore a good idea to rate limit a `vcan` interface to
prevent packet loss.

```
sudo tc qdisc add dev vcan0 root tbf rate 300kbit latency 100ms burst 1000
```
This command will rate limit `vcan0` to 300 kbit/s.
Try to match the rate limit with your physical interface on the remote.
Keep also in mind that this can also increases the overall latency!

Now start cannelloni on Machine 2:
```
cannelloni -I vcan0 -R 192.168.0.2 -r 20000 -l 20000
```

The tunnel is now complete and can be used on both machines. Simply try
it by using `candump` and/or `cangen`.

If something does not work, try the debug switch `-d cut` to find out
what is wrong.

###Timeouts

cannelloni either sends a full UDP frame or whatever it gots when
the timeout that has been specified by the `-t` flag is reached.
The default value is 100000 us, so the worst case latency for any can
frame is

```
Lw ~= 100ms + Ethernet latency + Delay on Receiver
```

If you have high priority frames but you also want a small ethernet 
overhead, you can create a csv in the format
```
CAN_ID,Timeout in us
```
to specify these frames. You can use the `#` character to comment your
frames.

For example, if the frames with the IDs 5 and 15 should be send after
a shorter timeout you can create a file with the following content.

```
# 15ms
5,15000
# 50ms
15,50000
```

You can load this file into each cannelloni instance with the `-T
file.csv` option.
Please note that the whole buffer will be flushed and not only the two
frames.

If you enable timer debugging using `-d t` you should see that the table
has been loaded successfully into cannelloni:

```
[...]
INFO:cannelloni.cpp[148]:main:Custom timeout table loaded:
INFO:cannelloni.cpp[149]:main:*---------------------*
INFO:cannelloni.cpp[150]:main:|  ID  | Timeout (us) |
INFO:cannelloni.cpp[153]:main:|     5|         15000|
INFO:cannelloni.cpp[153]:main:|    15|         50000|
INFO:cannelloni.cpp[154]:main:*---------------------*
INFO:cannelloni.cpp[155]:main:Other Frames:100000 us.
[...]
```

#Contributing
Please fork the repository, create a *separate* branch and create a PR
for your work.

#License

Copyright 2014-2015 Maximilian Güntner <maximilian.guentner@gmail.com>

cannelloni is licensed under the GPL, version 2. See gpl-2.0.txt for
more information.