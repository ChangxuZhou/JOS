//
// Created by tonny on 17-3-10.
//
#include <printf.h>
#include <stdio.h>

int main() {
    printf("%10.6d", 3);
    _printf("Strings:\n");

    const char *s = "Hello";
    _printf("\t[%10s]\n\t[%-10s]\n\t[%*s]\n\t[%-10.*s]\n\t[%-*.*s]\n",
            s, s, 10, s, 4, s, 10, 4, s);

    _printf("Characters:\t%c %%\n", 65);

    _printf("Integers\n");
    _printf("Decimal:\t%i %d %.6i %i %.0i %+i %u\n", 1, 2, 3, 0, 0, 4, -1);
    printf("Decimal:\t%i %d %.6i %i %.0i %+i %u\n", 1, 2, 3, 0, 0, 4, -1);
    _printf("Hexadecimal:\t%x %x %X %#x\n", 5, 10, 10, 6);
    _printf("Octal:\t%o %#o %#o\n", 10, 10, 4);

    _printf("Variable width control:\n");
    _printf("right-justified variable width: '%*c'\n", 5, 'x');
    _printf("left-justified variable width : '%*c'\n", -5, 'x');

    return 0;
}