deb_distro=bionic
DISTRO=stable
build_opts="-j 6"
build_opts="${build_opts} O=build_image/build"
build_opts="${build_opts} ARCH=arm64"
build_opts="${build_opts} KBUILD_DEBARCH=${DEBARCH}"
build_opts="${build_opts} LOCALVERSION=-carp-rk3328"
build_opts="${build_opts} KDEB_CHANGELOG_DIST=${deb_distro}"
build_opts="${build_opts} KDEB_PKGVERSION=1${DISTRO}"
build_opts="${build_opts} CROSS_COMPILE=aarch64-linux-gnu-" 	
build_opts="${build_opts} KDEB_SOURCENAME=linux-upstream"
make ${build_opts}  nanopi-r2_linux_defconfig
make ${build_opts}  
make ${build_opts}  bindeb-pkg