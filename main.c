#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h> // 【新增】DMA 神器
#include <stdio.h>
#include <string.h>

#define SHELL_PROMPT "root@c8t6:~# "
#define RX_BUF_SIZE 64
#define TX_BUF_SIZE 512 // 分配给 DMA 的专属发送内存区

char rx_buf[RX_BUF_SIZE];
uint8_t rx_idx = 0;

// DMA 专属内存（必须是全局变量，因为函数返回后 DMA 还在后台慢慢读它）
uint8_t dma_tx_buf[TX_BUF_SIZE]; 

/* ==========================================================
 * 魔法缝合区：用 DMA 劫持 printf 输出
 * ========================================================== */
int _write(int file, char *ptr, int len) {
    if (file != 1) return -1;

    // 1. 【防撞车机制】如果上一次 DMA 还没搬完，稍微等一下
    // (实际上如果配合环形缓冲区能做到完全不阻塞，这里为了代码极简先用线性等待)
    while (dma_get_number_of_data(DMA1, DMA_CHANNEL4) > 0);

    // 必须先关闭通道，才能写入新的传输配置
    dma_disable_channel(DMA1, DMA_CHANNEL4);

    // 2. 把数据快速搬运进 DMA 的专属内存，顺便处理回车换行
    int dma_len = 0;
    for (int i = 0; i < len && dma_len < TX_BUF_SIZE - 1; i++) {
        if (ptr[i] == '\n') {
            dma_tx_buf[dma_len++] = '\r';
        }
        dma_tx_buf[dma_len++] = ptr[i];
    }

    // 3. 【点火发射】给 DMA 分配任务并启动
    dma_set_memory_address(DMA1, DMA_CHANNEL4, (uint32_t)dma_tx_buf);
    dma_set_number_of_data(DMA1, DMA_CHANNEL4, dma_len);
    dma_enable_channel(DMA1, DMA_CHANNEL4);

    // CPU 瞬间返回，深藏功与名。此时后台的数据正在疯狂流向 USART！
    return len;
}

/* ==========================================================
 * 硬件初始化 (新增 DMA 配置)
 * ========================================================== */
static void system_setup(void) {
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

    // 【新增】使能 DMA1 时钟
    rcc_periph_clock_enable(RCC_DMA1);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_USART1);

    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
    gpio_set(GPIOC, GPIO13);

    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART1_TX);
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO_USART1_RX);

    usart_set_baudrate(USART1, 115200);
    usart_set_databits(USART1, 8);
    usart_set_stopbits(USART1, USART_STOPBITS_1);
    usart_set_mode(USART1, USART_MODE_TX_RX);
    usart_set_parity(USART1, USART_PARITY_NONE);
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

    // 【核心黑魔法：配置 DMA1 通道4 给 USART1_TX 用】
    dma_channel_reset(DMA1, DMA_CHANNEL4);
    // 目的地：USART1 的数据寄存器 (物理固定地址)
    dma_set_peripheral_address(DMA1, DMA_CHANNEL4, (uint32_t)&USART_DR(USART1));
    // 方向：从内存 (dma_tx_buf) 读出，发往外设
    dma_set_read_from_memory(DMA1, DMA_CHANNEL4);
    // 内存地址自增 (发完 buf[0] 发 buf[1])，外设地址不自增 (永远砸向 USART_DR)
    dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL4);
    dma_disable_peripheral_increment_mode(DMA1, DMA_CHANNEL4);
    // 每次搬运 8 bit (1 字节)
    dma_set_peripheral_size(DMA1, DMA_CHANNEL4, DMA_CCR_PSIZE_8BIT);
    dma_set_memory_size(DMA1, DMA_CHANNEL4, DMA_CCR_MSIZE_8BIT);
    // 优先级设为最高
    dma_set_priority(DMA1, DMA_CHANNEL4, DMA_CCR_PL_HIGH);

    // 【授权】让 USART1 允许 DMA 来接管发送
    usart_enable_tx_dma(USART1);
    
    usart_enable(USART1);
}

/* ==========================================================
 * Shell 命令解析器
 * ========================================================== */
static void execute_command(const char* cmd) {
    if (strlen(cmd) == 0) return;

    if (strcmp(cmd, "help") == 0) {
        printf("Available commands:\n");
        printf("  help    - Show this message\n");
        printf("  info    - System information\n");
        printf("  led on  - Turn on PC13 LED\n");
        printf("  led off - Turn off PC13 LED\n");
        printf("  stress  - DMA asynchronous stress test\n"); // 新增压测命令
    } 
    else if (strcmp(cmd, "info") == 0) {
        printf("[System] STM32F103C8T6\n");
        printf("[Clock]  72 MHz (HSE PLL)\n");
        printf("[Build]  libopencm3 baremetal (DMA Powered)\n");
    } 
    else if (strcmp(cmd, "led on") == 0) {
        gpio_clear(GPIOC, GPIO13);
        printf("PC13 LED is now ON.\n");
    } 
    else if (strcmp(cmd, "led off") == 0) {
        gpio_set(GPIOC, GPIO13);
        printf("PC13 LED is now OFF.\n");
    }
    // 【新增极限压测命令】
    else if (strcmp(cmd, "stress") == 0) {
        printf("DMA Stress Test started! This long string is being sent entirely by hardware DMA without blocking the CPU. The CPU is already executing the next instruction.\n");
    }
    else {
        printf("bash: %s: command not found\n", cmd);
    }
}

/* ==========================================================
 * 主循环
 * ========================================================== */
int main(void) {
    system_setup();

    printf("\n\n");
    printf("==================================\n");
    printf("  STM32 Shell V2.0 (DMA Edition)  \n");
    printf("==================================\n");
    printf(SHELL_PROMPT);

    while (1) {
        // 这里的单字符回显我们没做 DMA，是因为单个字符打断一下 CPU 无伤大雅
        // 但当你敲回车执行大段 printf 时，DMA 就会全面接管！
        if (usart_get_flag(USART1, USART_SR_RXNE)) {
            char c = usart_recv(USART1);
            if (c == '\n') continue; 

            if (c == '\r') {
                printf("\n"); 
                rx_buf[rx_idx] = '\0'; 
                execute_command(rx_buf);
                rx_idx = 0;   
                printf(SHELL_PROMPT);
            } 
            else if (c == '\b' || c == 0x7F) {
                if (rx_idx > 0) {
                    rx_idx--;
                    usart_send_blocking(USART1, '\b'); 
                    usart_send_blocking(USART1, ' ');  
                    usart_send_blocking(USART1, '\b'); 
                }
            } 
            else if (rx_idx < RX_BUF_SIZE - 1 && c >= 32 && c <= 126) {
                rx_buf[rx_idx++] = c;
                usart_send_blocking(USART1, c); 
            }
        }
    }
    return 0;
}