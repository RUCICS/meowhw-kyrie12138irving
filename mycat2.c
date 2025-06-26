#include <unistd.h> // 包含 read, write, open, close, sysconf 等系统调用
#include <fcntl.h>  // 包含文件控制选项，如 O_RDONLY
#include <stdio.h>  // 包含 perror, fprintf 函数
#include <stdlib.h> // 包含 exit, malloc, free 函数

// io_blocksize 函数：获取系统内存页大小作为IO缓冲区大小
// 返回值: 系统的内存页大小 (通常为 4KB 或 8KB)，如果获取失败则返回一个默认值 (4096)
size_t io_blocksize() {
    long page_size = sysconf(_SC_PAGESIZE); // 使用 sysconf 获取系统页大小
    if (page_size == -1) {
        // 如果 sysconf 返回 -1，表示获取失败（例如，_SC_PAGESIZE 不受支持）
        perror("警告: 无法获取系统页大小，将使用默认值 4096 字节");
        return 4096; // 默认缓冲区大小为 4KB
    }
    return (size_t)page_size; // 返回获取到的页大小
}

int main(int argc, char *argv[]) {
    int fd_in;           // 输入文件描述符
    char *buffer = NULL; // 缓冲区指针，初始化为NULL
    size_t buffer_size;  // 缓冲区大小
    ssize_t bytes_read;  // read() 函数返回的字节数
    ssize_t bytes_written; // write() 函数返回的字节数

    // 1. 检查命令行参数数量
    if (argc != 2) {
        fprintf(stderr, "用法: %s <文件名>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // 2. 获取缓冲区大小
    buffer_size = io_blocksize();
    fprintf(stderr, "使用缓冲区大小: %zu 字节 (系统页大小)\n", buffer_size);

    // 3. 动态分配缓冲区内存
    buffer = (char *)malloc(buffer_size);
    if (buffer == NULL) {
        // 如果 malloc 返回 NULL，表示内存分配失败
        perror("分配缓冲区内存失败");
        exit(EXIT_FAILURE);
    }

    // 4. 打开输入文件
    fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1) {
        perror("打开文件失败");
        free(buffer); // 释放已分配的内存
        exit(EXIT_FAILURE);
    }

    // 5. 循环读取文件内容到缓冲区，然后将缓冲区内容写入标准输出
    while ((bytes_read = read(fd_in, buffer, buffer_size)) > 0) {
        // read 函数尝试从 fd_in 读取 buffer_size 字节到 buffer 中
        // 如果 bytes_read > 0，表示成功读取到数据

        // write 函数尝试将 buffer 中实际读取到的 bytes_read 字节写入到标准输出
        bytes_written = write(STDOUT_FILENO, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            // 如果 write 返回的字节数不等于实际读取的字节数，表示写入失败或未完全写入
            perror("写入标准输出失败或未完全写入");
            close(fd_in); // 关闭文件
            free(buffer); // 释放内存
            exit(EXIT_FAILURE);
        }
    }

    // 6. 检查循环终止原因
    if (bytes_read == -1) {
        // 如果 read 函数返回 -1，表示读取过程中发生错误
        perror("读取文件失败");
        close(fd_in); // 关闭文件
        free(buffer); // 释放内存
        exit(EXIT_FAILURE);
    }

    // 7. 关闭文件
    if (close(fd_in) == -1) {
        perror("关闭文件失败");
        free(buffer); // 释放内存
        exit(EXIT_FAILURE);
    }

    // 8. 释放动态分配的缓冲区内存
    free(buffer);

    // 程序成功执行完毕
    return EXIT_SUCCESS;
}