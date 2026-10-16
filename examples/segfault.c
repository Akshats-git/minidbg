/* segfault.c - crashes on purpose by writing through a null pointer. */
#include <stdio.h>

void crash(int *p) {
    *p = 42;
}

int main(void) {
    printf("about to crash\n");
    crash(NULL);
    printf("not reached\n");
    return 0;
}
