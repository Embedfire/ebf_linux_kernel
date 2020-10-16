# 野火imx6ull系列开发板(linux kernel)

## 内核版本

    4.19.71

## 开发环境

**ubuntu18.04**

安装依赖工具：
```
sudo apt update
sudo apt install make gcc-arm-linux-gnueabihf gcc bison flex libssl-dev dpkg-dev lzop
```

**测试arm-none-eabi-gcc安装是否成功**

```bash
arm-none-eabi-gcc -v
```

**如果你的系统是64位的**

如果出现`No such file or directory`问题，可以用以下命令解决
```bash
sudo apt-get install lib32ncurses5 lib32tinfo5 libc6-i386
```
---

**开始编译**
```bash
./make_deb.sh
```

## 镜像输出路径

生成linux内核安装包将位于:

```bash
linux/build_image/linux-image-4.19.71-imx-r1_1stable_armhf.deb
```
[此安装包将用于构建完整的系统固件](https://embed-linux-tutorial.readthedocs.io/zh_CN/latest/building_image/building_debian.html)

---

## 其他信息

**Linux Kernel版本信息**

```
source from https://github.com/torvalds/linux
```

已适配野火imx6ull系列开发板