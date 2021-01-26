

dtbo-$(CONFIG_ARCH_STM32) += \
	stm-fire-bluetooth.dtbo \
	stm-fire-led.dtbo \

targets += dtbs dtbs_install
targets += $(dtbo-y)

always		:= $(dtbo-y)
clean-files	:= *.dtbo