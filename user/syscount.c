#include "kernel/types.h"
#include "user/user.h"

int main(void) {
    printf("Testing getsyscallcount()\n");

    // Make some syscalls
    getpid();
    fork();
    getpid();
    getpid();

    int count = getsyscallcount();
    printf("System calls made by this process: %d\n", count);

    exit(0);
}


