# Overlays for the Raspberry Pi platform
dtbo-$(CONFIG_SOC_IMX6ULL) += \
	imx-fire-18b20.dtbo \
	imx-fire-74hc595.dtbo \
	imx-fire-485r1.dtbo \
	imx-fire-485r2.dtbo \
	imx-fire-adc1.dtbo	\
	imx-fire-bluetooth.dtbo \
	imx-fire-btwifi.dtbo	\
	imx-fire-cam.dtbo	\
	imx-fire-can1.dtbo	\
	imx-fire-can2.dtbo	\
	imx-fire-dht11.dtbo	\
	imx-fire-ecspi3.dtbo	\
	imx-fire-ecspi3-slave.dtbo	\
	imx-fire-emmc.dtbo	\
	imx-fire-gpmi.dtbo	\
	imx-fire-hdmi.dtbo	\
	imx-fire-i2c-gpio.dtbo	\
	imx-fire-i2c1.dtbo	\
	imx-fire-i2c2.dtbo	\
	imx-fire-key.dtbo	\
	imx-fire-lcd.dtbo	\
	imx-fire-led.dtbo	\
	imx-fire-mpu6050.dtbo	\
	imx-fire-resistive-screen.dtbo	\
	imx-fire-sound.dtbo		\
	imx-fire-uart2.dtbo		\
	imx-fire-uart3.dtbo		\

targets += dtbs dtbs_install
targets += $(dtbo-y)

always		:= $(dtbo-y)
clean-files	:= *.dtbo
