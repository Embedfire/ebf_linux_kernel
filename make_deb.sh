deb_distro=bionic
DISTRO=stable
build_opts="-j 6"
build_opts="${build_opts} O=/home/pi/build"
build_opts="${build_opts} ARCH=arm"
build_opts="${build_opts} KBUILD_DEBARCH=${DEBARCH}"
build_opts="${build_opts} LOCALVERSION=-imx-r1"
build_opts="${build_opts} KDEB_CHANGELOG_DIST=${deb_distro}"
build_opts="${build_opts} KDEB_PKGVERSION=1${DISTRO}"	
build_opts="${build_opts} KDEB_SOURCENAME=linux-upstream"
make ${build_opts}  npi_v7_defconfig
make ${build_opts}  menuconfig
make ${build_opts}  
make ${build_opts}  bindeb-pkg
