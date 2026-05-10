/*
 * crash_ptr.c — Chương trình demo gây SIGSEGV (null pointer dereference)
 * Dùng trong buổi thuyết trình để minh họa phát hiện lỗi.
 *
 * Compile: gcc -o crash_ptr crash_ptr.c
 * Chạy   : ./crash_ptr
 */

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("Chuong trinh crash_ptr bat dau chay...\n");
    printf("Chuan bi doc bo nho tai dia chi NULL...\n");

    int *ptr = NULL;   /* Con tro null -- khong tro toi vung nho hop le */

    printf("Doc gia tri tai dia chi NULL: ");
    fflush(stdout);    /* Dam bao dong tren duoc in truoc khi crash */

    printf("%d\n", *ptr);  /* <-- Dong nay gay SIGSEGV */

    printf("Dong nay KHONG BAO GIO duoc in\n");
    return 0;
}
