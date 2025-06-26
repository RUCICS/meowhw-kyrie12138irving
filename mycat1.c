// mycat1.c - 一个逐字符读取和写入的简朴cat程序

#include <unistd.h> // 包含 read, write, open, close 等系统调用
#include <fcntl.h>  // 包含文件控制选项，如 O_RDONLY
#include <stdio.h>  // 包含 perror 函数，用于打印系统错误信息
#include <stdlib.h> // 包含 exit 函数

int main(int argc, char *argv[]) {
    int fd_in;       // 输入文件描述符
    char buffer[1];  // 每次只读取和写入一个字符的缓冲区
    ssize_t bytes_read; // read() 函数返回的字节数

    // 1. 检查命令行参数数量
    if (argc != 2) {
        // 如果参数数量不等于2（程序名本身算一个参数），则打印用法信息并退出
        fprintf(stderr, "用法: %s <文件名>\n", argv[0]);
        exit(EXIT_FAILURE); // 退出并返回失败状态码
    }

    // 2. 打开输入文件
    // O_RDONLY: 只读模式打开文件
    fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1) {
        // 如果 open 函数返回 -1，表示打开文件失败
        // perror 会根据全局变量 errno 打印相应的错误信息
        perror("打开文件失败");
        exit(EXIT_FAILURE);
    }

    // 3. 逐字符读取文件并写入标准输出
    while ((bytes_read = read(fd_in, buffer, 1)) > 0) {
        // read 函数尝试从 fd_in 读取 1 字节到 buffer 中
        // 如果 bytes_read > 0，表示成功读取到数据

        // write 函数尝试将 buffer 中的 1 字节写入到标准输出 (STDOUT_FILENO)
        if (write(STDOUT_FILENO, buffer, 1) != 1) {
            // 如果 write 返回的字节数不等于 1，表示写入失败
            perror("写入标准输出失败");
            close(fd_in); // 关闭已打开的文件描述符
            exit(EXIT_FAILURE);
        }
    }

    // 4. 检查循环终止原因
    if (bytes_read == -1) {
        // 如果 read 函数返回 -1，表示读取过程中发生错误
        perror("读取文件失败");
        close(fd_in); // 关闭已打开的文件描述符
        exit(EXIT_FAILURE);
    }

    // 5. 关闭文件
    if (close(fd_in) == -1) {
        // 如果 close 函数返回 -1，表示关闭文件失败
        perror("关闭文件失败");
        exit(EXIT_FAILURE);
    }

    // 程序成功执行完毕
    return EXIT_SUCCESS;
}