Dependencies

libsangoma (distributed with the Sangoma wanpipe drivers)
readline
pthreads

Compilation

Just type 'make', if the dependencies are installed it will create the binary wp-dchan-cli

Usage

./wp-dchan-cli -dev <s1c16> -nr

This opens the typical D-chan in an E1 device

./wp-dchan-cli -dev <s1c2>

This opens a data channel to the Sangoma GSM module to send AT commands


Why is this useful?

Not sure if useful for E1 lines, but for Sangoma GSM cards, that provide a fake data channel connected to the GSM serial
device, you can use it to send/recv raw AT commands to the chip.
