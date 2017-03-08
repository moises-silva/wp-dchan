Sangoma Wanpipe D-Channel Command Line
--------------------------------------

This gives you a command line to send/recv data from Sangoma D-channel devices.

Why is this useful?

Not sure if useful for real E1 lines, but for Sangoma GSM cards that provide a fake data HDLC (d-channel)
connected to the GSM serial device, you can use it to send/recv raw AT commands to the chip.


Dependencies
============

libsangoma (distributed with the Sangoma wanpipe drivers)
readline (yum install libreadline ?)
pthreads (pretty sure it's already installed in your Linux OS)

Compilation
===========

Just type 'make', if the dependencies are installed it will create the binary wp-dchan-cli

Usage::

    ./wp-dchan-cli -dev <s1c16> -nr

    This opens the typical D-chan in an E1 device

    ./wp-dchan-cli -dev <s1c2>

    This opens a data channel to the Sangoma GSM module to send AT commands


