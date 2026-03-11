#include <stdint.h>
#include <stdio.h>   // 引入标准输入输出库 (printf, sscanf)
#include <string.h>  // 引入字符串处理库 (strcmp, strncmp)
#include <sys/stat.h> // 用于 _fstat 的标准结构体
#include <sys/types.h> // 必须引入这个头文件以支持 ptrdiff_t

// --- 寄存器基地址定义 ---
#define RCC_BASE      0x40021000
#define RCC_CR        (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR      (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define FLASH_ACR     (*(volatile uint32_t *)(0x40022000 + 0x00))

// --- 链接脚本导出的符号 ---
extern uint32_t _estack, _sdata, _edata, _etext, _sbss, _ebss;

// --- uart.c 导出的底层硬件接口 ---
extern void uart_init(void);
extern void uart_putc(char c);
extern char uart_getc(void);

// ==========================================
// 终极系统调用桩函数 (Syscall Stubs)
// 完全接管 Newlib-nano 的底层，彻底消灭所有 Warning
// ==========================================

// 这是你刚才在 link.ld 里加的那个“路标”
extern char end; 

// 真正的动态内存分配底层引擎
void *_sbrk(ptrdiff_t incr) {
    extern char end; 
    static char *heap_end = 0;
    char *prev_heap_end;

    if (heap_end == 0) {
        heap_end = &end;
    }

    // 【防暴毙装甲】：强制堆指针向 8 字节对齐
    heap_end = (char *)(((uint32_t)heap_end + 7) & ~7);

    prev_heap_end = heap_end;
    heap_end += incr;
    
    return (void *)prev_heap_end;
}

// 1. 我们最核心的输出重定向
int _write(int file, char *ptr, int len) {
    for (int i = 0; i < len; i++) {
        uart_putc(ptr[i]);
    }
    return len;
}

// 2. 以下全部是安抚编译器的空壳，保证签名完全符合 POSIX 标准
int _close(int file) { 
    return -1; 
}

int _fstat(int file, struct stat *st) { 
    st->st_mode = S_IFCHR; // 告诉库：我们是一个字符设备（串口）
    return 0; 
}

int _isatty(int file) { 
    return 1; 
}

int _lseek(int file, int ptr, int dir) { 
    return 0; 
}

int _read(int file, char *ptr, int len) { 
    return 0; 
}

// 甚至连这两个防患于未然的也加上
void _exit(int status) { 
    while(1); 
}

int _kill(int pid, int sig) { 
    return -1; 
}

int _getpid(void) { 
    return 1; 
}

// ==========================================
// Shell 指令解析引擎 (体验标准库带来的优雅)
// ==========================================
void process_cmd(char *cmd) {
    if (strcmp(cmd, "help") == 0) {
        printf("Commands:\r\n");
        printf("  help                - Show this message\r\n");
        printf("  uid                 - Read 96-bit Unique ID\r\n");
        printf("  read <hex>          - Read 32-bit value at address\r\n");
        printf("  write <hex> <hex>   - Write 32-bit value to address\r\n");
    } 
    else if (strcmp(cmd, "uid") == 0) {
        uint32_t *uid = (uint32_t *)0x1FFFF7E8;
        // 一行搞定 96位 ID 的零填充十六进制打印
        printf("Chip UID: %08X %08X %08X\r\n", 
               (unsigned int)uid[2], (unsigned int)uid[1], (unsigned int)uid[0]);
    } 
    else if (strncmp(cmd, "read ", 5) == 0) {
        unsigned int addr;
        // sscanf 轻松解析输入的十六进制地址
        if (sscanf(cmd + 5, "%x", &addr) == 1) {
            addr &= ~3; // 强制 4 字节对齐，防止硬件总线异常 (HardFault)
            uint32_t val = *(volatile uint32_t *)addr;
            printf("Value at 0x%08X = 0x%08X\r\n", addr, (unsigned int)val);
        } else {
            printf("Error: Invalid address format. Usage: read 0x08000000\r\n");
        }
    }
    else if (strncmp(cmd, "write ", 6) == 0) {
        unsigned int addr, val;
        // 同时解析地址和要写入的值
        if (sscanf(cmd + 6, "%x %x", &addr, &val) == 2) {
            addr &= ~3; 
            *(volatile uint32_t *)addr = val; // 【极度危险且迷人】直接修改硬件内存！
            printf("Written 0x%08X to 0x%08X\r\n", val, addr);
        } else {
            printf("Error: Invalid format. Usage: write 0x20000000 0x12345678\r\n");
        }
    }
    else if (cmd[0] != '\0') {
        printf("Unknown Command: %s. Type 'help'.\r\n", cmd);
    }
}

// ==========================================
// 系统初始化与主循环
// ==========================================
void main(void) {
    // 1. 时钟树初始化 (72MHz)
    FLASH_ACR = 0x12;
    RCC_CR |= (1 << 16); 
    while (!(RCC_CR & (1 << 17))); 
    RCC_CFGR = (0x07 << 18) | (1 << 16); 
    RCC_CR |= (1 << 24); 
    while (!(RCC_CR & (1 << 25)));
    RCC_CFGR |= 0x02; 
    while ((RCC_CFGR & 0x0C) != 0x08);

    // 2. 串口硬件初始化
    uart_init();

    setvbuf(stdout, NULL, _IONBF, 0);
    
    // 3. 打印开机横幅 (现在可以用 printf 了！)
    printf("\r\n\r\n=== Baremetal OS Booted ===\r\n");
    printf("Compiled with Newlib-nano\r\n");
    printf("stm32 > ");

    char cmd_buf[64];
    int cmd_idx = 0;

    // 4. 交互式 Shell 死循环
    while (1) {
        char c = uart_getc();
        if (c != 0) {
            // 回显字符
            uart_putc(c); 
            
            if (c == '\r' || c == '\n') { 
                printf("\n");
                cmd_buf[cmd_idx] = '\0'; 
                process_cmd(cmd_buf);    
                cmd_idx = 0;             
                printf("stm32 > ");
            } 
            else if (c == '\b' || c == 0x7F) { // 完美处理退格键
                if (cmd_idx > 0) {
                    cmd_idx--;
                    // 终端光标回退，空格覆盖，再回退
                    printf(" \b"); 
                }
            } 
            else if (cmd_idx < 63) {
                cmd_buf[cmd_idx++] = c;  
            }
        }
    }
}

// ==========================================
// 裸机启动逻辑：复位处理函数
// ==========================================
// 顶部声明也要改一下，加上 _sidata
extern uint32_t _sidata, _sdata, _edata, _etext, _sbss, _ebss;

void Reset_Handler(void) {
    // 【核心修复】：使用 LOADADDR 导出的绝对物理地址
    uint32_t *src = &_sidata; 
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;
    main();
    while(1);
}

// ==========================================
// 中断向量表
// ==========================================
__attribute__((section(".isr_vector")))
void (*const vector_table[16])(void) = {
    [0] = (void (*)(void))&_estack,
    [1] = Reset_Handler,
};


// make clean && make
// gdb-multiarch shell.elf -ex "so load.gdb"

// make clean && make && gdb-multiarch shell.elf -ex "so load.gdb"