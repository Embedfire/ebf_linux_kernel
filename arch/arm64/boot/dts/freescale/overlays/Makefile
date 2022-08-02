# Overlays for the imx8mm platform

dtbo-$(CONFIG_ARCH_MXC) += \
	imx8mm-fire-rx8010sj.dtbo \
	imx8mm-fire-uart3.dtbo \
	imx8mm-fire-rs485.dtbo \
	imx8mm-fire-btwifi.dtbo \
	imx8mm-fire-mipi.dtbo \
	imx8mm-fire-cam.dtbo \
	imx8mm-fire-sound.dtbo \
	imx8mm-fire-touch-capacitive-goodix.dtbo \
	imx8mm-fire-hdmi.dtbo \
	imx8mm-fire-pcie.dtbo \
	imx8mm-fire-keys.dtbo \
	imx8mm-fire-leds.dtbo \
	imx8mm-fire-rpmsg.dtbo \
	imx8mm-fire-pwm1.dtbo \
	imx8mm-fire-pwm2.dtbo \
	imx8mm-fire-spi.dtbo \

targets += dtbs dtbs_install dtbs_check
targets += $(dtbo-y)

always		:= $(dtbo-y)

