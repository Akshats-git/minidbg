/* factorial.c - recursive factorial. Breaking inside it gives a deep stack for bt. */
#include <stdio.h>

long factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    long result = factorial(5);
    printf("factorial(5) = %ld\n", result);
    return 0;
}
