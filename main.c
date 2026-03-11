#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/cm3/scb.h>

/* ==========================================================
 * 全局变量与 CLI 配置
 * ========================================================== */
usbd_device *g_usbd_dev = NULL; // 留给 printf 用的后门指针

#define CLI_PROMPT "STM32-USB> "
#define RX_BUF_SIZE 64
char rx_buf[RX_BUF_SIZE];
uint8_t rx_idx = 0;

/* ==========================================================
 * 缝合标准库：让 printf 走 USB 发送
 * ========================================================== */
int _write(int file, char *ptr, int len) {
    if (file != 1 || !g_usbd_dev) return -1;
    
    char buf[128];
    int idx = 0;
    for (int i = 0; i < len; i++) {
        // 将换行符转为串口标准的回车换行
        if (ptr[i] == '\n') {
            buf[idx++] = '\r';
        }
        buf[idx++] = ptr[i];
        
        // USB 批量传输(Bulk)的最大包长通常是 64 字节，快满时就发一包
        if (idx >= 64) {
            usbd_ep_write_packet(g_usbd_dev, 0x82, buf, idx);
            idx = 0;
        }
    }
    // 发送剩余尾巴
    if (idx > 0) {
        usbd_ep_write_packet(g_usbd_dev, 0x82, buf, idx);
    }
    return len;
}

/* ==========================================================
 * CLI 命令解析器
 * ========================================================== */
static void parse_serial_command(const char* cmd) {
    if (strlen(cmd) == 0) return;

    if (strcmp(cmd, "help") == 0) {
        printf("Available commands:\n");
        printf("  help    - Show this message\n");
        printf("  info    - System hardware info\n");
        printf("  led on  - Turn on PC13 LED\n");
        printf("  led off - Turn off PC13 LED\n");
        printf("  reset   - Software reset MCU\n");
    } 
    else if (strcmp(cmd, "info") == 0) {
        printf("[System] STM32F103C8T6\n");
        printf("[Clock]  72 MHz (HSE PLL)\n");
        printf("[Bus]    Native USB 2.0 Full Speed CDC\n");
    } 
    else if (strcmp(cmd, "led on") == 0) {
        gpio_clear(GPIOC, GPIO13);
        printf("PC13 LED: ON\n");
    } 
    else if (strcmp(cmd, "led off") == 0) {
        gpio_set(GPIOC, GPIO13);
        printf("PC13 LED: OFF\n");
    }
    else if (strcmp(cmd, "reset") == 0) {
        printf("Rebooting...\n");
        for (volatile int i = 0; i < 100000; i++); // 稍作延时让USB包发完
        scb_reset_system(); // 触发内核软复位
    }
    else {
        printf("Command not found: %s\n", cmd);
    }
}

/* ==========================================================
 * USB Descriptors (身份户口本)
 * ========================================================== */
static const struct usb_device_descriptor dev_descr = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = USB_CLASS_CDC,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,
    .idProduct = 0x5740,
    .bcdDevice = 0x0200,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const struct usb_endpoint_descriptor comm_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = 0x83, .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize = 16, .bInterval = 255,
}};

static const struct usb_endpoint_descriptor data_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = 0x01, .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 64, .bInterval = 1,
}, {
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = 0x82, .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 64, .bInterval = 1,
}};

static const struct {
    struct usb_cdc_header_descriptor header;
    struct usb_cdc_call_management_descriptor call_mgmt;
    struct usb_cdc_acm_descriptor acm;
    struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
    .header = { .bFunctionLength = sizeof(struct usb_cdc_header_descriptor), .bDescriptorType = CS_INTERFACE, .bDescriptorSubtype = USB_CDC_TYPE_HEADER, .bcdCDC = 0x0110, },
    .call_mgmt = { .bFunctionLength = sizeof(struct usb_cdc_call_management_descriptor), .bDescriptorType = CS_INTERFACE, .bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT, .bmCapabilities = 0, .bDataInterface = 1, },
    .acm = { .bFunctionLength = sizeof(struct usb_cdc_acm_descriptor), .bDescriptorType = CS_INTERFACE, .bDescriptorSubtype = USB_CDC_TYPE_ACM, .bmCapabilities = 0, },
    .cdc_union = { .bFunctionLength = sizeof(struct usb_cdc_union_descriptor), .bDescriptorType = CS_INTERFACE, .bDescriptorSubtype = USB_CDC_TYPE_UNION, .bControlInterface = 0, .bSubordinateInterface0 = 1, }
};

static const struct usb_interface_descriptor comm_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0, .bAlternateSetting = 0, .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_CDC, .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
    .bInterfaceProtocol = USB_CDC_PROTOCOL_AT, .iInterface = 0,
    .endpoint = comm_endp, .extra = &cdcacm_functional_descriptors,
    .extralen = sizeof(cdcacm_functional_descriptors)
}};

static const struct usb_interface_descriptor data_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 1, .bAlternateSetting = 0, .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_DATA, .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0, .iInterface = 0, .endpoint = data_endp,
}};

static const struct usb_interface ifaces[] = {{.num_altsetting = 1, .altsetting = comm_iface}, {.num_altsetting = 1, .altsetting = data_iface}};

static const struct usb_config_descriptor config = {
    .bLength = USB_DT_CONFIGURATION_SIZE, .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0, .bNumInterfaces = 2, .bConfigurationValue = 1,
    .iConfiguration = 0, .bmAttributes = 0x80, .bMaxPower = 0x32, .interface = ifaces,
};

static const char *usb_strings[] = { "My SSD Company", "STM32 USB CLI", "DEMO0001" };

/* ==========================================================
 * USB 回调与状态机处理
 * ========================================================== */
uint8_t usbd_control_buffer[128];

// 1. 数据接收回调：在这里处理键盘敲击
static void cdcacm_data_rx_cb(usbd_device *usbd_dev, uint8_t ep) {
    (void)ep;
    char buf[64];
    int len = usbd_ep_read_packet(usbd_dev, 0x01, buf, 64);

    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') continue; // 忽略多余换行防抖

        if (c == '\r') {
            printf("\n");
            rx_buf[rx_idx] = '\0';
            parse_serial_command(rx_buf);
            rx_idx = 0;
            printf(CLI_PROMPT);
        }
        else if (c == '\b' || c == 0x7F) {
            if (rx_idx > 0) {
                rx_idx--;
                printf("\b \b"); // 终端退格
            }
        }
        else if (rx_idx < RX_BUF_SIZE - 1 && c >= 32 && c <= 126) {
            rx_buf[rx_idx++] = c;
            printf("%c", c); // 键入回显
        }
    }
}

// 2. Windows 握手敷衍回调
static enum usbd_request_return_codes cdcacm_control_cb(
    usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
    uint16_t *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req)) {
    (void)complete; (void)buf; (void)usbd_dev;

    switch (req->bRequest) {
    case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
        return USBD_REQ_HANDLED;
    case USB_CDC_REQ_SET_LINE_CODING:
        if (*len < sizeof(struct usb_cdc_line_coding)) return USBD_REQ_NOTSUPP;
        return USBD_REQ_HANDLED;
    }
    return USBD_REQ_NEXT_CALLBACK;
}

// 3. 配置注册回调
static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue) {
    (void)wValue;
    usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_rx_cb);
    usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, 64, NULL);
    usbd_ep_setup(usbd_dev, 0x83, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

    usbd_register_control_callback(usbd_dev,
        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        cdcacm_control_cb);
}

/* ==========================================================
 * 主函数
 * ========================================================== */
int main(void) {
    // 1. 初始化时钟与外设
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_USB);

    // 2. 初始化 PC13 LED
    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
    gpio_set(GPIOC, GPIO13); // 默认拉高熄灭

    // 3. USB 断电重连魔法 (强行拉低 PA12)
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
    gpio_clear(GPIOA, GPIO12);
    for (int i = 0; i < 800000; i++) __asm__("nop"); 

    // 4. 启动 USB 协议栈
    usbd_device *usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev_descr, &config,
                                      usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
    usbd_register_set_config_callback(usbd_dev, cdcacm_set_config);

    // 【极其关键】赋值给全局指针，赋予 printf 灵魂
    g_usbd_dev = usbd_dev;

    // 5. 主循环维持状态机
    while (1) {
        usbd_poll(usbd_dev);
    }
    return 0;
}