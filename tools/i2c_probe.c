/*
 * i2c_probe.c —— 单地址 I2C 探测（M2-02 用，避免全总线盲扫）
 *
 * 为什么不用 i2cdetect -y N：
 *   本板触摸 gslx680 挂在 I2C 上，全量扫描可能把某些器件探进异常状态。
 *   这个工具只对指定地址做一次“读一字节”，确认从机是否 ACK。
 *
 * 交叉编译（VM）：
 *   arm-linux-gcc -O2 -o build/i2c_probe tools/i2c_probe.c
 *   # 或静态：arm-linux-gcc -static -O2 -o build/i2c_probe tools/i2c_probe.c
 *
 * 板端：
 *   ./i2c_probe /dev/i2c-1 0x73
 *   返回 0 = ACK（器件在线）；1 = NAK；2 = 打开/参数错误。
 *
 * 注意：只探 0x08..0x77 的 7 位地址；不要拿它扫整条总线。
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

int main(int argc, char **argv)
{
    const char *dev;
    int addr, fd;
    unsigned char byte = 0;
    ssize_t n;

    if (argc < 3) {
        fprintf(stderr, "用法: %s /dev/i2c-N 0x73\n", argv[0]);
        return 2;
    }
    dev = argv[1];
    addr = (int)strtol(argv[2], NULL, 0);
    if (addr < 0x08 || addr > 0x77) {
        fprintf(stderr, "地址必须是 7 位 0x08..0x77（不是 8 位写地址）\n");
        return 2;
    }

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror(dev);
        return 2;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("I2C_SLAVE");
        close(fd);
        return 2;
    }

    n = read(fd, &byte, 1);
    if (n == 1) {
        printf("ACK  0x%02x on %s  (first byte 0x%02x)\n", addr, dev, byte);
        close(fd);
        return 0;
    }

    printf("NAK  0x%02x on %s\n", addr, dev);
    close(fd);
    return 1;
}
