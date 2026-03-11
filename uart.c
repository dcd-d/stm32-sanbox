#include <stdint.h>

// 寄存器基地址
#define RCC_APB2ENR   (*(volatile uint32_t *)(0x40021000 + 0x18))
#define GPIOA_CRH     (*(volatile uint32_t *)(0x40010800 + 0x04))
#define USART1_BASE   0x40013800
#define USART1_SR     (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DR     (*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR    (*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CR1    (*(volatile uint32_t *)(USART1_BASE + 0x0C))

void uart_init(void) {
    // 1. 开启 GPIOA 和 USART1 的时钟 (位2 和 位14)
    RCC_APB2ENR |= (1 << 2) | (1 << 14);

    // 2. 配置 PA9 (TX) 和 PA10 (RX)
    // PA9 配置为复用推挽输出 50MHz (CNF=10, MODE=11 -> 0xB)
    // PA10 配置为浮空输入 (CNF=01, MODE=00 -> 0x4)
    GPIOA_CRH &= ~0x00000FF0; // 清空 PA9 和 PA10 的配置位
    GPIOA_CRH |=  0x000004B0; // 写入新配置

    // 3. 配置波特率: 115200
    // USART1 挂载在 APB2 总线上 (72MHz)
    // 算法: 72,000,000 / 115200 = 625 (0x0271)
    USART1_BRR = 0x0271;

    // 4. 使能串口：UE=1(使能), TE=1(发送使能), RE=1(接收使能)
    USART1_CR1 = (1 << 13) | (1 << 3) | (1 << 2);
}

// 发送一个字符
void uart_putc(char c) {
    // 等待发送数据寄存器为空 (TXE)
    while (!(USART1_SR & (1 << 7)));
    USART1_DR = c;
}

// 发送字符串
void uart_puts(const char *str) {
    while (*str) uart_putc(*str++);
}

// 非阻塞接收一个字符
char uart_getc(void) {
    // 检查读数据寄存器是否非空 (RXNE)
    if (USART1_SR & (1 << 5)) {
        return (char)(USART1_DR & 0xFF);
    }
    return 0; // 没收到数据就返回 0
}