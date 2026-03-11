PROJECT = firmware

# 1. 你的手术刀（交叉编译工具链）
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

# 2. 芯片架构参数 (Cortex-M3)
ARCH_FLAGS = -mcpu=cortex-m3 -mthumb

# 3. 编译选项 (包含子模块头文件，定义 STM32F1 宏)
CFLAGS = $(ARCH_FLAGS) -Os -Wall -I./libopencm3/include -DSTM32F1

# 4. 链接选项 (终极缝合的灵魂所在)
LDFLAGS = $(ARCH_FLAGS) -nostartfiles -T stm32f103c8t6.ld \
          -L./libopencm3/lib -lopencm3_stm32f1 -Wl,--gc-sections \
          --specs=nano.specs --specs=nosys.specs

# 5. 生产流水线
all: $(PROJECT).bin

$(PROJECT).elf: main.c
	$(CC) $(CFLAGS) main.c $(LDFLAGS) -o $@

$(PROJECT).bin: $(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin