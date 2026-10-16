/* loop.c - a loop with a counter. Used to try breakpoints and stepping. */
#include <stdio.h>

int bump(int counter) {
    return counter + 1;
}

int main(void) {
    int counter = 0;
    for (int i = 0; i < 5; i++) {
        counter = bump(counter);
        printf("counter = %d\n", counter);
    }
    return 0;
}
