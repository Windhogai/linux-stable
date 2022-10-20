#!/bin/sh
# LDD5 build script
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
make socfpga_defconfig
make drivers/misc/ledpwm.ko
touch .scmversion