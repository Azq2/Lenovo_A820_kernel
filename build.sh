#!/bin/bash

set -e

DIR=$(readlink -f $(dirname $0))

# From https://developer.arm.com/downloads/-/gnu-rm
export CROSS_COMPILE="ccache /opt/gcc-arm-none-eabi-5_3-2016q1/bin/arm-none-eabi-"
export ARCH=arm
export CONFIG_KERNEL_UNDERVOLT=yes
export CUSTOM_KERNEL_VERSION=ProtonKernel-v2.57RC2-uv
export KBUILD_BUILD_USER=build
export KBUILD_BUILD_HOST=localhost
export TARGET_BUILD_VARIANT=user
export LD_PRELOAD=

./makeMtk -t lenovo_a820 mrproper k
./makeMtk -t lenovo_a820 c k

./mk -t -o=TARGET_BUILD_VARIANT=user lenovo_a820 n k

