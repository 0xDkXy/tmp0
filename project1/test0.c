#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>

#define NR_ADD_TO_EXTENT 449

int main(int argc, char** argv)
{
    syscall(NR_ADD_TO_EXTENT, 1, 2);
    return 0;
}
