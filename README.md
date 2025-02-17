# Memory Allocator

## Overview

This project is a custom implementation of a dynamic memory allocator in C, designed to manage memory efficiently at a low level. It replicates core memory management functions like `malloc()`, `calloc()`, `realloc()`, and `free()`, offering a deeper understanding of system memory operations. 

The memory allocator is designed to work with the Linux operating system and interacts directly with system calls to manage memory at a fine-grained level. It supports both heap expansion through `brk()` and large block allocations using `mmap()`, allowing efficient memory usage and reducing system call overhead.

## Features

- **Dynamic Memory Allocation**: Provides memory allocation and deallocation functionalities similar to standard C library functions.
- **System Calls Integration**: Utilizes Linux system calls such as `brk()`, `mmap()`, and `munmap()` to allocate and manage memory effectively.
- **Memory Block Management**: Implements splitting and coalescing of memory blocks to minimize fragmentation.
- **Efficient Free List Management**: Tracks free blocks using a doubly linked list to facilitate fast allocation and reuse.
- **8-Byte Alignment**: Ensures all memory allocations are aligned to 8-byte boundaries, improving performance and memory efficiency.
- **Heap Preallocation**: Optimizes memory allocation by reserving a large memory chunk upfront to reduce frequent system calls.
- **Error Handling**: Includes robust error checking for system calls and memory operations to ensure stability and reliability.
- **Best-Fit Strategy**: Allocates memory blocks efficiently by selecting the smallest available block that fits the requested size, reducing fragmentation over time.
- **Block Coalescing**: Merges adjacent free blocks upon deallocation to optimize memory reuse and reduce fragmentation.
- **Debugging Support**: Implements logging and validation checks to help track memory leaks and improper usage of the allocator.

## Repository Structure

- `src/` - Core implementation of the memory allocator.
- `tests/` - Test suite to validate functionality and detect memory leaks.
- `utils/` - Supporting utility functions and header files for better modularity.

## Why This Project?

This project showcases a strong understanding of memory management, low-level programming, and system calls. It highlights skills in optimizing memory allocation, working with data structures, and debugging complex memory issues. The implementation focuses on reducing fragmentation and improving allocation efficiency, making it a valuable learning experience in systems programming and performance optimization.

By working on this project, I have gained hands-on experience with memory alignment, free list management, system call integration, and performance tuning. This knowledge is directly applicable to developing high-performance applications, embedded systems, and operating system components.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
