deb_distro=bionic
DISTRO=stable
KERNEL_DIR=$(pwd)
build_opts="-j 8"
build_opts="${build_opts} O=build_image/build"
build_opts="${build_opts} ARCH=arm64"
build_opts="${build_opts} KERNEL_DIR=${KERNEL_DIR}"
build_opts="${build_opts} KBUILD_DEBARCH=${DEBARCH}"
build_opts="${build_opts} LOCALVERSION=-imx8mm"
build_opts="${build_opts} KDEB_CHANGELOG_DIST=${deb_distro}"
build_opts="${build_opts} KDEB_PKGVERSION=1.$(date +%g%m)${DISTRO}"
build_opts="${build_opts} CROSS_COMPILE=aarch64-linux-gnu-" 	
build_opts="${build_opts} KDEB_SOURCENAME=linux-upstream"
make ${build_opts}  imx_v8_defconfig
make ${build_opts}  
make ${build_opts}  bindeb-pkg