// mycat4.c - 一个使用页对齐缓冲区，并考虑文件系统块大小的cat程序

#include <unistd.h>     // 包含 read, write, open, close, sysconf 等系统调用
#include <fcntl.h>      // 包含文件控制选项，如 O_RDONLY
#include <stdio.h>      // 包含 perror, fprintf 函数
#include <stdlib.h>     // 包含 exit, malloc, free 函数
#include <stdint.h>     // 包含 uintptr_t，用于指针和整数之间的安全转换
#include <sys/stat.h>   // 包含 fstat 和 struct stat，用于获取文件信息

// get_system_page_size 函数：获取系统内存页大小
// 返回值: 系统的内存页大小，如果获取失败则返回一个默认值 (4096)
long get_system_page_size() {
    long page_size = sysconf(_SC_PAGESIZE); // 使用 sysconf 获取系统页大小
    if (page_size == -1) {
        perror("警告: 无法获取系统页大小，将使用默认值 4096 字节");
        return 4096; // 默认缓冲区大小为 4KB
    }
    return page_size;
}

// io_blocksize 函数：根据系统内存页大小和文件系统块大小确定最佳IO缓冲区大小
// 参数: fd - 文件的文件描述符，用于获取文件系统块大小
// 返回值: 推荐的缓冲区大小
size_t io_blocksize(int fd) {
    long page_size = get_system_page_size(); // 获取系统内存页大小

    struct stat st;
    long fs_block_size = 0; // 文件系统块大小，初始化为0

    if (fstat(fd, &st) == 0) {
        // st_blksize 是文件系统建议的最佳I/O块大小
        // 它是文件系统为了高效读写该文件而推荐的块大小。
        fs_block_size = st.st_blksize;
    } else {
        perror("警告: 无法获取文件系统块大小，将只使用内存页大小");
    }

    size_t recommended_size;
    if (fs_block_size > 0) {
        // 如果成功获取到文件系统块大小，则优先使用它
        // 因为这是文件系统为该文件优化的块大小。
        recommended_size = (size_t)fs_block_size;
    } else {
        // 如果无法获取文件系统块大小（例如，文件描述符无效），则回退到系统页大小。
        recommended_size = (size_t)page_size;
    }

    // 确保最终的缓冲区大小至少是内存页大小。
    // 即使文件系统的推荐块大小小于页大小，我们也使用页大小，以确保页对齐带来的效益。
    if (recommended_size < (size_t)page_size) {
        recommended_size = (size_t)page_size;
    }

    return recommended_size;
}

// align_alloc 函数：分配一段内存，长度不小于 size 并且返回一个对齐到内存页起始的指针
// 参数: size - 需要分配的最小字节数
// 返回值: 对齐到内存页起始的指针，如果分配失败则返回 NULL
char* align_alloc(size_t size) {
    // 获取系统页大小，用于内存对齐计算。
    // 这里我们只需要物理内存页大小，与文件系统块大小无关。
    size_t page_size = (size_t)get_system_page_size();

    // 我们需要分配额外的空间来存储原始的 malloc 指针，以及确保有足够的空间进行对齐。
    // sizeof(void*): 用于存储原始 malloc 返回的指针，以便 align_free 可以找到它。
    // page_size - 1: 确保无论原始 malloc 指针在哪，我们总能在这个额外空间内找到一个页对齐的地址。
    char *original_ptr = (char *)malloc(size + page_size - 1 + sizeof(void*));
    if (original_ptr == NULL) {
        return NULL; // 内存分配失败
    }

    // 计算页对齐后的地址：
    // 1. original_ptr + sizeof(void*): 跳过用于存储原始指针的空间。
    // 2. + page_size - 1: 向上取整的技巧。
    // 3. & ~(page_size - 1): 使用位运算将地址的低位清零，使其对齐到 page_size 的倍数。
    uintptr_t aligned_addr_val = ((uintptr_t)(original_ptr + sizeof(void*)) + page_size - 1) & ~(page_size - 1);
    char *aligned_ptr = (char*)aligned_addr_val;

    // 将原始的 malloc 返回的指针存储在对齐地址的前面 sizeof(void*) 的位置。
    // 这样 align_free 就可以根据对齐后的指针计算出原始指针的位置并释放它。
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

    // 3. 获取缓冲区大小（在文件打开后调用 io_blocksize，因为它需要文件描述符）
    buffer_size = io_blocksize(fd_in);
    fprintf(stderr, "使用缓冲区大小: %zu 字节 (系统页大小和文件系统块大小取大者)\n", buffer_size);

    // 4. 使用 align_alloc 动态分配页对齐的缓冲区内存
    buffer = align_alloc(buffer_size);
    if (buffer == NULL) {
        perror("分配页对齐缓冲区内存失败");
        close(fd_in); // 在退出前关闭文件
        exit(EXIT_FAILURE);
    }

    // 5. 循环读取文件内容到缓冲区，然后将缓冲区内容写入标准输出
    while ((bytes_read = read(fd_in, buffer, buffer_size)) > 0) {
        // read 函数尝试从 fd_in 读取 buffer_size 字节到 buffer 中。

        // write 函数尝试将 buffer 中实际读取到的 bytes_read 字节写入到标准输出。
        bytes_written = write(STDOUT_FILENO, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            // 如果 write 返回的字节数不等于实际读取的字节数，表示写入失败或未完全写入。
            perror("写入标准输出失败或未完全写入");
            close(fd_in);       // 关闭文件
            align_free(buffer); // 释放内存
            exit(EXIT_FAILURE);
        }
    }

    // 6. 检查循环终止原因
    if (bytes_read == -1) {
        // 如果 read 函数返回 -1，表示读取过程中发生错误。
        perror("读取文件失败");
        close(fd_in);       // 关闭文件
        align_free(buffer); // 释放内存
        exit(EXIT_FAILURE);
    }

    // 7. 关闭文件
    if (close(fd_in) == -1) {
        perror("关闭文件失败");
        align_free(buffer); // 释放内存
        exit(EXIT_FAILURE);
    }

    // 8. 释放动态分配的缓冲区内存
    align_free(buffer);

    // 程序成功执行完毕
    return EXIT_SUCCESS;
}