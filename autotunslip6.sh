#!/bin/sh

cd /home/ubuntu/contiki/tools/
sudo /home/ubuntu/contiki/tools/tunslip6 -B 115200 -s /dev/ttyUSB0 aaaa::1/64
