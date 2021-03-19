# Overlays for the Raspberry Pi platform
dtbo-$(CONFIG_SOC_IMX6ULL) += \
	imx-fire-ds18b20.dtbo \
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
	imx-fire-ecspi1.dtbo	\
	imx-fire-ecspi2.dtbo	\
	imx-fire-ecspi3.dtbo	\
	imx-fire-ecspi3-slave.dtbo	\
	imx-fire-ecspi4.dtbo	\
	imx-fire-emmc.dtbo	\
	imx-fire-gpmi.dtbo	\
	imx-fire-hdmi.dtbo	\
	imx-fire-otg1.dtbo	\
	imx-fire-i2c-gpio.dtbo	\
	imx-fire-i2c1.dtbo	\
	imx-fire-i2c2.dtbo	\
	imx-fire-i2c3.dtbo	\
	imx-fire-i2c4.dtbo	\
	imx-fire-key.dtbo	\
	imx-fire-lcd.dtbo	\
	imx-fire-led.dtbo	\
	imx-fire-mpu6050.dtbo	\
	imx-fire-pwm-led.dtbo	\
	imx-fire-pwm.dtbo \
	imx-fire-resistive-screen.dtbo	\
	imx-fire-sound.dtbo		\
	imx-fire-touch-capacitive-goodix.dtbo	\
	imx-fire-touch-resisitive-4wires.dtbo	\
	imx-fire-uart2.dtbo		\
	imx-fire-uart3.dtbo		\
	imx-fire-uart4.dtbo		\
	imx-fire-uart5.dtbo		\
	imx-fire-uart6.dtbo		\
	imx-fire-uart7.dtbo		\
	imx-fire-uart8.dtbo		\

targets += dtbs dtbs_install
targets += $(dtbo-y)

always		:= $(dtbo-y)
clean-files	:= *.dtbo
