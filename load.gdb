# tar ext 192.168.1.3:2331
# load
# compare-sections
# set $pc = Reset_Handler
# c




# 1. 基础连接与复位
target extended-remote 192.168.1.3:2331

# 修正：去掉 kHz，只保留数字
monitor speed 4000
# monitor reset halt

# 2. 界面显示优化
# 修正：去掉行尾注释，防止解析器混淆
set print pretty on
set pagination off
set confirm off

# 保存历史记录
set history save on

# 3. 烧录与校验
echo \n--- Loading Binary ---\n
load
compare-sections

# 4. 自动显示
# 修正：给指令之间增加空格，增加兼容性
display /i $pc

# 5. 定义快捷指令宏
define reload
    monitor reset halt
    load
    compare-sections
    set $pc = Reset_Handler
    echo \n--- Reloaded and Reset to Handler ---\n
end

# 6. 初始断点配置
set $pc = Reset_Handler

# b main

c