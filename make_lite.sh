#!/bin/bash

ARCH="arm"
CROSS_COMPILE="arm-linux-gnueabihf-"
JOBS="-j16"
CONFIG="npi_v7_lite_defconfig"

if [ $# -eq 0 ]; then
    echo "请提供编译参数，可选参数: all, modules, dtbs, zImage, cleanall"
    exit 1
fi

# 加载配置文件
config_load() {
    make ARCH=$ARCH $CONFIG
}

# 编译全部
build_all() {
    config_load
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE $JOBS
    package_modules
}

# 单独编译内核
build_kernel() {
    config_load
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE zImage $JOBS
}

# 单独编译设备树
build_dtbs() {
    config_load
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE dtbs $JOBS
}

# 单独编译驱动模块并打包
build_modules() {
    config_load
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules $JOBS
    package_modules
}

# 打包驱动模块
package_modules() {
    if [ ! -e "./modules" ]; then
        mkdir modules
    fi
    rm -rf modules/*
    make modules_install INSTALL_MOD_PATH=modules
    cd modules/lib/modules
    tar -jcvf ../../modules.tar.bz2 .
    cd -
    rm -rf modules/lib
}

# 清除编译输出
build_cleanall() {
    make mrproper
}

case $1 in
    all)
        build_all
        ;;
    modules)
        build_modules
        ;;
    dtbs)
        build_dtbs
        ;;
    zImage)
        build_kernel
        ;;
    cleanall)
        build_cleanall
        ;;
    *)
        echo "无效的参数，可选参数: all, modules, dtbs, zImage, cleanall"
        exit 1
        ;;
esac