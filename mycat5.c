// mycat5.c - A cat program using an experimentally determined optimal fixed buffer size.

#include <unistd.h>     // For read, write, open, sysconf system calls
#include <fcntl.h>      // For file control options, like O_RDONLY
#include <stdio.h>      // For perror, fprintf functions
#include <stdlib.h>     // For exit, malloc, free functions
#include <stdint.h>     // For uintptr_t, used for safe pointer-to-integer conversions
#include <errno.h>      // For errno, used for error handling

// Define the experimentally determined optimal buffer size (2MB).
// This value is based on experimental measurements of system call overhead.
#define OPTIMAL_BUFFER_SIZE (2 * 1024 * 1024) // 2MB

// get_system_page_size function: Retrieves the system's memory page size.
// This is a helper function used in align_alloc for page alignment calculations.
// Returns: The system's memory page size, or a default value (4096) if retrieval fails.
long get_system_page_size() {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) {
        perror("Warning: Could not get system page size, using default 4096 bytes for alignment");
        return 4096;
    }
    return page_size;
}

// io_blocksize function: Returns the experimentally determined optimal buffer size.
// This function no longer dynamically adjusts based on file system or page size,
// but returns a fixed optimized value.
size_t io_blocksize() {
    return OPTIMAL_BUFFER_SIZE;
}

// align_alloc function: Allocates memory not less than 'size' and returns a pointer
// aligned to a memory page boundary.
// Parameters: size - The minimum number of bytes to allocate.
// Returns: A pointer aligned to a memory page boundary, or NULL if allocation fails.
char* align_alloc(size_t size) {
    // Get the system page size for memory alignment calculation.
    size_t page_size = (size_t)get_system_page_size();

    // We need to allocate extra space to store the original malloc pointer,
    // and to ensure enough space for alignment.
    // sizeof(void*): To store the original pointer returned by malloc, so align_free can find it.
    // page_size - 1: To ensure that no matter where the original malloc pointer is,
    // we can always find a page-aligned address within this extra space.
    char *original_ptr = (char *)malloc(size + page_size - 1 + sizeof(void*));
    if (original_ptr == NULL) {
        return NULL; // Memory allocation failed
    }

    // Calculate the page-aligned address:
    // 1. original_ptr + sizeof(void*): Skip the space used to store the original pointer.
    // 2. + page_size - 1: A trick for rounding up, ensuring that adding this value
    //    will cross to the next or current page boundary, regardless of the current address.
    // 3. & ~(page_size - 1): Use bitwise AND with the bitwise NOT of (page_size - 1).
    //    This clears the lower bits of the address, effectively aligning it to a multiple of page_size.
    //    (e.g., if page_size is 4096 (0x1000), then page_size - 1 is 4095 (0xFFF).
    //    ~(page_size - 1) is ~0xFFF, which will clear the lower 12 bits of the address,
    //    aligning it to a multiple of 4096).
    uintptr_t aligned_addr_val = ((uintptr_t)(original_ptr + sizeof(void*)) + page_size - 1) & ~(page_size - 1);
    char *aligned_ptr = (char*)aligned_addr_val;

    // Store the original malloc-returned pointer in the space immediately preceding
    // the aligned address (sizeof(void*) bytes before it).
    // This allows align_free to calculate the original pointer's location and free it.
    *((char**)(aligned_ptr - sizeof(void*))) = original_ptr;

    return aligned_ptr;
}

// align_free function: Frees memory previously returned by align_alloc.
// Parameters: ptr - The page-aligned pointer returned by align_alloc.
void align_free(void* ptr) {
    if (ptr == NULL) {
        return; // Handle NULL pointer to avoid crashes.
    }
    // Retrieve the original malloc-returned pointer from the space immediately
    // preceding the aligned address (sizeof(void*) bytes before it).
    char *original_ptr = *((char**)((char*)ptr - sizeof(void*)));
    free(original_ptr); // Free the original, malloc-allocated memory block.
}

int main(int argc, char *argv[]) {
    int fd_in;           // Input file descriptor
    char *buffer = NULL; // Pointer to the buffer
    size_t buffer_size;  // Size of the buffer
    ssize_t bytes_read;  // Number of bytes returned by read()
    ssize_t bytes_written; // Number of bytes returned by write()

    // 1. Check command-line argument count.
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // 2. Open the input file.
    fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    // 3. Get the buffer size (now a fixed value).
    buffer_size = io_blocksize();
    fprintf(stderr, "Using experimentally determined optimal fixed buffer size: %zu bytes\n", buffer_size);

    // 4. Dynamically allocate page-aligned buffer memory using align_alloc.
    buffer = align_alloc(buffer_size);
    if (buffer == NULL) {
        perror("Failed to allocate page-aligned buffer memory");
        close(fd_in); // Close the file before exiting.
        exit(EXIT_FAILURE);
    }

    // 5. Loop to read file content into the buffer, then write buffer content to standard output.
    while ((bytes_read = read(fd_in, buffer, buffer_size)) > 0) {
        // read function attempts to read buffer_size bytes from fd_in into buffer.

        // write function attempts to write bytes_read bytes from buffer to standard output.
        bytes_written = write(STDOUT_FILENO, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            // If the number of bytes written is not equal to the number of bytes read,
            // it indicates a write failure or incomplete write.
            perror("Failed to write to standard output or incomplete write");
            close(fd_in);       // Close the file
            align_free(buffer); // Free memory
            exit(EXIT_FAILURE);
        }
    }

    // 6. Check the reason for loop termination.
    if (bytes_read == -1) {
        // If read function returns -1, it indicates an error during reading.
        perror("Failed to read file");
        close(fd_in);       // Close the file
        align_free(buffer); // Free memory
        exit(EXIT_FAILURE);
    }

    // 7. Close the file.
    if (close(fd_in) == -1) {
        perror("Failed to close file");
        align_free(buffer); // Free memory
        exit(EXIT_FAILURE);
    }

    // 8. Free the dynamically allocated buffer memory.
    align_free(buffer);

    // Program executed successfully.
    return EXIT_SUCCESS;
}
