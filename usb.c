#include <stdint.h>

// --- USB 硬件寄存器定义 ---
#define USB_BASE      0x40005C00
#define USB_EP0R      (*(volatile uint16_t *)(USB_BASE + 0x00))
#define USB_EP1R      (*(volatile uint16_t *)(USB_BASE + 0x04))
#define USB_EP2R      (*(volatile uint16_t *)(USB_BASE + 0x08))
#define USB_EP3R      (*(volatile uint16_t *)(USB_BASE + 0x0C))
#define USB_CNTR      (*(volatile uint16_t *)(USB_BASE + 0x40))
#define USB_ISTR      (*(volatile uint16_t *)(USB_BASE + 0x44))
#define USB_FNR       (*(volatile uint16_t *)(USB_BASE + 0x48))
#define USB_DADDR     (*(volatile uint16_t *)(USB_BASE + 0x4C))
#define USB_BTABLE    (*(volatile uint16_t *)(USB_BASE + 0x50))
#define PMA_BASE      0x40006000

// PMA 缓冲区分配表 (使用 16 位偏移地址，实际内存为 32 位间隔)
#define BTABLE_ADDR   0x0000
#define EP0_TX_ADDR   0x0040
#define EP0_RX_ADDR   0x0080
#define EP1_TX_ADDR   0x00C0 // 用于发数据给电脑
#define EP2_RX_ADDR   0x0100 // 用于接收电脑输入
#define EP3_TX_ADDR   0x0140 // 用于 CDC 中断

// --- 端点寄存器操作宏 (STM32 USB 寄存器翻转机制极其变态) ---
#define EP_TOGGLE_SET(ep_reg, bits, mask) \
    (ep_reg) = (((ep_reg) ^ (bits)) & (mask | 0x8080)) | 0x8080

// --- 完整的 USB CDC 描述符 ---
const uint8_t dev_desc[] = {
    0x12, 0x01, 0x00, 0x02, 0x02, 0x00, 0x00, 0x40, 
    0x83, 0x04, 0x40, 0x57, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01
};

const uint8_t cfg_desc[] = {
    // Configuration Descriptor
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
    // Interface 0 (Comm)
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    // CDC Functional Descriptors
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,
    // Endpoint 3 (Interrupt IN)
    0x07, 0x05, 0x83, 0x03, 0x08, 0x00, 0xFF,
    // Interface 1 (Data)
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    // Endpoint 1 (Data IN) - TX
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
    // Endpoint 2 (Data OUT) - RX
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};

// 简单的全局状态变量
static uint8_t dev_addr = 0;
char rx_buf[64];
int rx_ready = 0;

// --- PMA 读写函数 ---
void pma_write(uint16_t offset, const uint8_t *data, uint16_t len) {
    uint32_t *pma = (uint32_t *)(PMA_BASE + offset * 2);
    for (uint16_t i = 0; i < len; i += 2) {
        *pma++ = data[i] | (data[i + 1] << 8);
    }
}

void pma_read(uint16_t offset, uint8_t *data, uint16_t len) {
    uint32_t *pma = (uint32_t *)(PMA_BASE + offset * 2);
    for (uint16_t i = 0; i < len; i += 2) {
        uint32_t val = *pma++;
        data[i] = val & 0xFF;
        if (i + 1 < len) data[i + 1] = (val >> 8) & 0xFF;
    }
}

// --- 初始化与 API ---
void usb_init(void) {
    USB_CNTR = 3; for(volatile int i=0; i<1000; i++); USB_CNTR = 0;
    USB_ISTR = 0; 
    USB_BTABLE = BTABLE_ADDR;
    // 开启复位和正确传输中断
    USB_CNTR = (1<<15) | (1<<10);
    // NVIC 使能 USB 中断 (IRQ 20)
    *(volatile uint32_t *)(0xE000E100) |= (1 << 20);
}

void usb_send_str(const char *str) {
    uint16_t len = 0;
    while (str[len] && len < 64) len++;
    pma_write(EP1_TX_ADDR, (const uint8_t *)str, len);
    *(volatile uint16_t *)(USB_BASE + 0x50 + 1*8 + 0) = len; // EP1 TX Count
    EP_TOGGLE_SET(USB_EP1R, 0x0010, 0x0030); // 设为 VALID，开始发送
    for(volatile int i=0; i<10000; i++); // 极简等待
}

int usb_get_cmd(char *buf) {
    if (rx_ready) {
        for(int i=0; i<64; i++) {
            buf[i] = rx_buf[i];
            if(buf[i] == '\r' || buf[i] == '\n') { buf[i] = '\0'; break; }
        }
        rx_ready = 0;
        // 重新开启 EP2 接收
        EP_TOGGLE_SET(USB_EP2R, 0x3000, 0x3000); 
        return 1;
    }
    return 0;
}

// --- 核心状态机：处理电脑发来的枚举请求 ---
void handle_ep0_setup(void) {
    uint8_t req[8];
    pma_read(EP0_RX_ADDR, req, 8); // 读取 8 字节 SETUP 包
    uint8_t req_type = req[0], request = req[1];
    uint16_t value = req[2] | (req[3] << 8);
    uint16_t length = req[6] | (req[7] << 8);

    const uint8_t *tx_data = 0;
    uint16_t tx_len = 0;

    if (req_type == 0x80 && request == 0x06) { // GET_DESCRIPTOR
        if ((value >> 8) == 1) { tx_data = dev_desc; tx_len = sizeof(dev_desc); }
        else if ((value >> 8) == 2) { tx_data = cfg_desc; tx_len = sizeof(cfg_desc); }
        if (tx_len > length) tx_len = length;
        if (tx_len > 64) tx_len = 64; // 只处理单包发送，简化逻辑
    } 
    else if (req_type == 0x00 && request == 0x05) { // SET_ADDRESS
        dev_addr = value; 
    } 
    else if (req_type == 0x00 && request == 0x09) { // SET_CONFIGURATION
        // 配置数据端点 EP1(TX) 和 EP2(RX)
        USB_EP1R = 0x0220; // BULK_IN
        *(volatile uint16_t *)(USB_BASE + 0x50 + 1*8 + 4) = EP1_TX_ADDR;
        
        USB_EP2R = 0x3200; // BULK_OUT
        *(volatile uint16_t *)(USB_BASE + 0x50 + 2*8 + 4) = EP2_RX_ADDR;
        *(volatile uint16_t *)(USB_BASE + 0x50 + 2*8 + 6) = 0x8400; // RX_COUNT: 64B块
        EP_TOGGLE_SET(USB_EP2R, 0x3000, 0x3000); // EP2 设为 VALID
    }
    else if (req_type == 0x21 && request == 0x20) { // CDC SET_LINE_CODING (Windows必需)
        // 假装处理，直接回复状态
    }

    // 回复控制包 (Status Stage / Data Stage)
    if (tx_len > 0) {
        pma_write(EP0_TX_ADDR, tx_data, tx_len);
    }
    *(volatile uint16_t *)(USB_BASE + 0x50 + 0) = tx_len; // EP0 TX COUNT
    EP_TOGGLE_SET(USB_EP0R, 0x0010, 0x0030); // EP0_TX 设为 VALID
}

// --- 硬件中断入口 ---
void usb_isr(void) {
    uint16_t istr = USB_ISTR;
    
    // 1. 复位中断 (线缆刚插上)
    if (istr & (1<<10)) { 
        USB_ISTR &= ~(1<<10);
        // 初始化 BTABLE 指针
        volatile uint16_t *btable = (volatile uint16_t *)(USB_BASE + 0x50);
        btable[0] = EP0_TX_ADDR; btable[1] = 0;
        btable[2] = EP0_RX_ADDR; btable[3] = 0x8400; // 64字节块大小
        
        USB_DADDR = 0x80; // Enable USB, Address 0
        USB_EP0R = 0x3220; // CONTROL 类型，RX_VALID, TX_NAK
    }
    
    // 2. 正确传输中断
    if (istr & (1<<15)) { 
        uint8_t ep = istr & 0x0F;
        uint16_t ep_val;
        
        if (ep == 0) { // 端点 0 控制传输
            ep_val = USB_EP0R;
            if (ep_val & 0x0800) { // SETUP 包到达
                handle_ep0_setup();
                EP_TOGGLE_SET(USB_EP0R, 0, 0x0800); // 清除 SETUP 标志
            } 
            else if (ep_val & 0x8000) { // RX 完成 (例如 Status 阶段)
                EP_TOGGLE_SET(USB_EP0R, 0, 0x8000); 
                EP_TOGGLE_SET(USB_EP0R, 0x3000, 0x3000); // 继续监听 RX
            } 
            else if (ep_val & 0x0080) { // TX 完成
                EP_TOGGLE_SET(USB_EP0R, 0, 0x0080);
                if (dev_addr != 0) {
                    USB_DADDR = 0x80 | dev_addr; // 地址设置必须在 TX 完成后生效
                    dev_addr = 0;
                }
            }
        } 
        else if (ep == 2) { // 端点 2 接收到用户的键盘输入
            ep_val = USB_EP2R;
            if (ep_val & 0x8000) { // RX 完成
                uint16_t count = *(volatile uint16_t *)(USB_BASE + 0x50 + 2*8 + 6) & 0x3FF;
                pma_read(EP2_RX_ADDR, (uint8_t*)rx_buf, count);
                rx_buf[count] = '\0';
                rx_ready = 1;
                EP_TOGGLE_SET(USB_EP2R, 0, 0x8000); // 清除标志，此处不立即开启接收，交由主循环
            }
        }
        else if (ep == 1) { // 端点 1 发送完成
            ep_val = USB_EP1R;
            if (ep_val & 0x0080) {
                EP_TOGGLE_SET(USB_EP1R, 0, 0x0080); // 清除标志
            }
        }
    }
}