// mycat6.c - 一个使用实验确定最佳固定缓冲区大小，并使用posix_fadvise进行优化的cat程序

#include <unistd.h>     // 包含 read, write, open 等系统调用
#include <fcntl.h>      // 包含文件控制选项，如 O_RDONLY, posix_fadvise
#include <stdio.h>      // 包含 perror, fprintf 函数
#include <stdlib.h>     // 包含 exit, malloc, free 函数
#include <stdint.h>     // 包含 uintptr_t，用于指针和整数之间的安全转换
#include <errno.h>      // 包含 errno，用于错误处理

// 定义实验确定的最佳缓冲区大小 (2MB)
// 这个值是基于对系统调用开销的实验测量得出的。
#define OPTIMAL_BUFFER_SIZE (2 * 1024 * 1024) // 2MB

// get_system_page_size 函数：获取系统内存页大小
// 这是一个辅助函数，用于 align_alloc 中的页对齐计算。
// 返回值: 系统的内存页大小，如果获取失败则返回一个默认值 (4096)
long get_system_page_size() {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) {
        perror("警告: 无法获取系统页大小，将使用默认值 4096 字节进行对齐");
        return 4096;
    }
    return page_size;
}

// io_blocksize 函数：返回实验确定的最佳缓冲区大小
// 此函数不再根据文件系统或页大小动态调整，而是返回一个固定的优化值。
size_t io_blocksize() {
    return OPTIMAL_BUFFER_SIZE;
}

// align_alloc 函数：分配一段内存，长度不小于 size 并且返回一个对齐到内存页起始的指针
// 参数: size - 需要分配的最小字节数
// 返回值: 对齐到内存页起始的指针，如果分配失败则返回 NULL
char* align_alloc(size_t size) {
    // 获取系统页大小，用于内存对齐计算。
    size_t page_size = (size_t)get_system_page_size();

    // 我们需要分配额外的空间来存储原始的 malloc 指针，以及确保有足够的空间进行对齐。
    char *original_ptr = (char *)malloc(size + page_size - 1 + sizeof(void*));
    if (original_ptr == NULL) {
        return NULL; // 内存分配失败
    }

    // 计算页对齐后的地址：
    uintptr_t aligned_addr_val = ((uintptr_t)(original_ptr + sizeof(void*)) + page_size - 1) & ~(page_size - 1);
    char *aligned_ptr = (char*)aligned_addr_val;

    // 将原始的 malloc 返回的指针存储在对齐地址的前面 sizeof(void*) 的位置。
    *((char**)(aligned_ptr - sizeof(void*))) = original_ptr;

    return aligned_ptr;
}

// align_free 函数：释放先前从 align_alloc 返回的内存
// 参数: ptr - 从 align_alloc 返回的页对齐指针
void align_free(void* ptr) {
    if (ptr == NULL) {
        return; // 处理 NULL 指针，避免崩溃
    }
    // 从对齐地址的前面 sizeof(void*) 的位置获取原始 malloc 返回的指针。
    char *original_ptr = *((char**)((char*)ptr - sizeof(void*)));
    free(original_ptr); // 释放原始的、由 malloc 分配的内存块。
}

int main(int argc, char *argv[]) {
    int fd_in;           // 输入文件描述符
    char *buffer = NULL; // 缓冲区指针
    size_t buffer_size;  // 缓冲区大小
    ssize_t bytes_read;  // read() 函数返回的字节数
    ssize_t bytes_written; // write() 函数返回的字节数

    // 1. 检查命令行参数数量
    if (argc != 2) {
        fprintf(stderr, "用法: %s <文件名>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // 2. 打开输入文件
    fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1) {
        perror("打开文件失败");
        exit(EXIT_FAILURE);
    }

    // 3. 使用 posix_fadvise 提示文件系统进行顺序读取优化
    // fd: 文件描述符
    // offset: 0，从文件开头开始
    // len: 0，表示从 offset 到文件结尾
    // advice: POSIX_FADV_SEQUENTIAL，表示文件将以顺序方式读取
    if (posix_fadvise(fd_in, 0, 0, POSIX_FADV_SEQUENTIAL) == -1) {
        // posix_fadvise 失败通常不是致命错误，打印警告即可。
        // 例如，某些文件系统或内核版本可能不支持此功能。
        perror("警告: posix_fadvise (POSIX_FADV_SEQUENTIAL) 失败");
    } else {
        fprintf(stderr, "已使用 posix_fadvise(POSIX_FADV_SEQUENTIAL) 提示文件系统。\n");
    }

    // 4. 获取缓冲区大小（现在是固定值）
    buffer_size = io_blocksize();
    fprintf(stderr, "使用实验确定的最佳固定缓冲区大小: %zu 字节\n", buffer_size);

    // 5. 使用 align_alloc 动态分配页对齐的缓冲区内存
    buffer = align_alloc(buffer_size);
    if (buffer == NULL) {
        perror("分配页对齐缓冲区内存失败");
        close(fd_in);
        exit(EXIT_FAILURE);
    }

    // 6. 循环读取文件内容到缓冲区，然后将缓冲区内容写入标准输出
    while ((bytes_read = read(fd_in, buffer, buffer_size)) > 0) {
        bytes_written = write(STDOUT_FILENO, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("写入标准输出失败或未完全写入");
            close(fd_in);
            align_free(buffer);
            exit(EXIT_FAILURE);
        }
    }

    // 7. 检查循环终止原因
    if (bytes_read == -1) {
        perror("读取文件失败");
        close(fd_in);
        align_free(buffer);
        exit(EXIT_FAILURE);
    }

    // 8. 关闭文件
    if (close(fd_in) == -1) {
        perror("关闭文件失败");
        align_free(buffer);
        exit(EXIT_FAILURE);
    }

    // 9. 释放动态分配的缓冲区内存
    align_free(buffer);

    // 程序成功执行完毕
    return EXIT_SUCCESS;
}