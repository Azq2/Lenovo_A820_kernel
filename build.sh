#!/bin/bash

set -e

DIR=$(readlink -f $(dirname $0))

# From https://developer.arm.com/downloads/-/gnu-rm
export CROSS_COMPILE="ccache /opt/gcc-arm-none-eabi-5_3-2016q1/bin/arm-none-eabi-"

export ARCH=arm
export TARGET_PRODUCT=lenovo89_cu_jb
export TARGET_BUILD_VARIANT=user
export KBUILD_BUILD_USER=build
export KBUILD_BUILD_HOST=localhost
export MTK_ROOT_CUSTOM="$DIR/mediatek/custom/"
export KERNEL_OUT_DIR="$DIR/out"
export CCACHE_DIR="$KERNEL_OUT_DIR/ccache"
export RELEASE_OUT_DIR="$DIR/release"
unset LD_PRELOAD

[[ -d $KERNEL_OUT_DIR ]] || mkdir $KERNEL_OUT_DIR
[[ -d $RELEASE_OUT_DIR ]] || mkdir $RELEASE_OUT_DIR
[[ -d $CCACHE_DIR ]] || mkdir $CCACHE_DIR

echo "CROSS_COMPILE=$CROSS_COMPILE"

cp -v "$DIR/kernel/mediatek-configs" "$KERNEL_OUT_DIR/.config"

make -j$((`nproc` + 1)) -C "$DIR/kernel" O="$KERNEL_OUT_DIR"
make -j$((`nproc` + 1)) -C "$DIR/kernel" O="$KERNEL_OUT_DIR" savedefconfig
make -j$((`nproc` + 1)) -C "$DIR/kernel" O="$KERNEL_OUT_DIR" \
	INSTALL_MOD_STRIP=--strip-unneeded INSTALL_MOD_PATH="$RELEASE_OUT_DIR" INSTALL_MOD_PATH="$RELEASE_OUT_DIR" android_modules_install
cp -v "$KERNEL_OUT_DIR/arch/arm/boot/zImage" "$RELEASE_OUT_DIR/zImage"

tree "$RELEASE_OUT_DIR"
