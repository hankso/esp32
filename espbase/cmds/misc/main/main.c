/*
 * File: main.c
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2026/8/2 08:15:48
 */

#include <stdio.h>
#include <unistd.h>

int fib(int n, char **unused) {
    if (n < 0) return -1;
    if (n <= 1) return n;
    int a = 0, b = 1, c;
    for (int i = 2; i <= n; i++) {
        c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int test(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        printf("FIB(%d) = %d\n", i, fib(i, NULL));
        sleep(1);
    }
    return 0;
}
