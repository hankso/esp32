/*
 * File: main.c
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2025/3/5 13:04:27
 */

#include <stdio.h>

int main(int argc, char **argv) {
    printf("Hello world!\n");
    for (int i = 0; i < argc; i++) {
        printf("Got arg[%d]=%s\n", i + 1, argv[i]);
    }
    return 0;
}
