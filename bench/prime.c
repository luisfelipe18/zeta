/* prime.c — Count primes up to N (C reference implementation for the benchmark)
   Compile: gcc -O2 -o prime prime.c
   Usage:   ./prime --n 500000
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_prime(int n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    for (int i = 3; i * i <= n; i += 2)
        if (n % i == 0) return 0;
    return 1;
}

static int count_primes(int limit) {
    int count = 0;
    for (int num = 2; num <= limit; num++)
        if (is_prime(num)) count++;
    return count;
}

int main(int argc, char *argv[]) {
    int n = 500000;
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--n") == 0)
            n = atoi(argv[i + 1]);

    printf("Primes up to %d: %d\n", n, count_primes(n));
    return 0;
}
