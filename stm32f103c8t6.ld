/* 划定 C8T6 的物理地盘 */
MEMORY
{
    rom (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
/* 缝合术：直接引入 libopencm3 写好的底层段排布规则 */
INCLUDE libopencm3/lib/cortex-m-generic.ld