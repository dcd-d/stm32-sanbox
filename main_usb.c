#include <stdint.h>

// --- 裸机寄存器基地址映射 ---
#define RCC_BASE 0x40021000
#define RCC_CR (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x1C))

#define FLASH_ACR (*(volatile uint32_t *)(0x40022000 + 0x00))
#define GPIOA_CRH (*(volatile uint32_t *)(0x40010800 + 0x04))
#define GPIOA_BSRR (*(volatile uint32_t *)(0x40010800 + 0x10))

// --- 外部符号与函数声明 ---
extern uint32_t _estack, _sdata, _edata, _etext, _sbss, _ebss;
extern void usb_init(void);
extern void usb_isr(void);
extern void usb_send_str(const char *str);
extern int usb_get_cmd(char *buf);

// --- Shell 命令处理核心 ---
void process_cmd(char *cmd)
{
    if (cmd[0] == 'h')
    {
        usb_send_str("Commands: help, temp, uid\r\n");
    }
    else if (cmd[0] == 'u')
    {
        usb_send_str("UID Read Success.\r\n");
    }
    else if (cmd[0] == 't')
    {
        usb_send_str("CPU Temp: ~30 C\r\n");
    }
    else if (cmd[0] != '\0')
    {
        usb_send_str("Unknown Command.\r\n");
    }
    usb_send_str("stm32 > ");
}

void main(void)
{
    // 1. 配置系统时钟：HSE(8MHz) -> PLL -> 72MHz
    FLASH_ACR = 0x12;    // 设置 Flash 延迟
    RCC_CR |= (1 << 16); // 开启外部晶振 (HSE)
    while (!(RCC_CR & (1 << 17)))
        ; // 等待 HSE 稳定

    // 配置倍频: PLL=x9 (72MHz), USB时钟=72/1.5=48MHz (必须是48MHz)
    RCC_CFGR = (0x07 << 18) | (1 << 16) | (1 << 22);
    RCC_CR |= (1 << 24); // 开启 PLL
    while (!(RCC_CR & (1 << 25)))
        ; // 等待 PLL 稳定

    RCC_CFGR |= 0x02; // 切换系统时钟源为 PLL
    while ((RCC_CFGR & 0x0C) != 0x08)
        ;

    // 2. 开启 GPIOA 和 USB 外设时钟
    RCC_APB2ENR |= (1 << 2);
    RCC_APB1ENR |= (1 << 23);

    // 3. 强制触发 USB 重新枚举 (控制 PA12 拉低一段时间再浮空)
    GPIOA_CRH &= ~(0xF << 16);
    GPIOA_CRH |= (0x3 << 16); // PA12 设为推挽输出
    GPIOA_BSRR = (1 << 28);   // 拉低 PA12 (D+ 数据线)
    for (volatile int i = 0; i < 720000; i++)
        ; // 延时约 10ms
    GPIOA_CRH &= ~(0xF << 16);
    GPIOA_CRH |= (0x4 << 16); // PA12 恢复浮空输入，交由 USB 硬件接管

    // 4. 初始化 USB 寄存器
    usb_init();

    // 5. 进入主循环
    char cmd_buf[64];
    while (1)
    {
        if (usb_get_cmd(cmd_buf))
        {
            process_cmd(cmd_buf);
        }
    }
}

// --- 裸机启动逻辑：复位处理函数 ---
void Reset_Handler(void)
{
    // 将初始化好的变量从 Flash 搬运到 RAM
    uint32_t *src = &_etext;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    // 清零 BSS 段
    dst = &_sbss;
    while (dst < &_ebss)
        *dst++ = 0;

    // 跳转到 C 语言入口
    main();
    while (1)
        ;
}

// --- 中断向量表 (利用 C99 指定初始化器精确排位) ---
__attribute__((section(".isr_vector"))) void (*const vector_table[53])(void) = {
    [0] = (void (*)(void))&_estack, // 栈顶指针
    [1] = Reset_Handler,            // 系统复位中断
    [36] = usb_isr,                 // USB_LP_CAN_RX0_IRQn (位置 16+20=36)
};
