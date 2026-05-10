/*
 * crash_div.c — Chương trình demo gây SIGFPE (divide by zero)
 * Dùng trong buổi thuyết trình để minh họa phát hiện lỗi.
 *
 * Compile: gcc -o crash_div crash_div.c
 * Chạy   : ./crash_div
 */

#include <stdio.h>

int main(void)
{
    printf("Chuong trinh crash_div bat dau chay...\n");
    printf("Chuan bi tinh phep chia...\n");

    int a = 10;
    int b = 0;   /* Co tinh dat b = 0 de gay loi */

    printf("Tinh %d / %d = ", a, b);
    fflush(stdout);  /* Dam bao dong tren duoc in truoc khi crash */

    int c = a / b;   /* <-- Dong nay gay SIGFPE */

    printf("%d\n", c);
    printf("Dong nay KHONG BAO GIO duoc in\n");
    return 0;
}
