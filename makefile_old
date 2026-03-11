CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

# 编译参数：针对 Cortex-M3，开启 O2 优化，移除所有标准库依赖
CFLAGS = -mthumb -mcpu=cortex-m3 -Os -g -ffreestanding -Wall
LDFLAGS = -T link.ld -nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs

all: shell.bin

# 链接两个 C 文件
# shell.elf: main.c usb.c
# 	$(CC) $(CFLAGS) $(LDFLAGS) main.c usb.c -o $@

shell.elf: main.c uart.c
	$(CC) $(CFLAGS) $(LDFLAGS) main.c uart.c -o $@

# 提取纯二进制数据
shell.bin: shell.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin