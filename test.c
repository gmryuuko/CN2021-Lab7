#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

void* foo(void *arg) {
    int num = *((int*)arg);
    printf("enter foo %d.\n", num);
    printf("leave foo %d.\n", num);
}

int main(int argc, char** argv) {
    unsigned char ch = 0xff;
    printf("%x\n", ch);
    return 0;

    printf("enter main.\n");

    pthread_t tid1, tid2;

    int err;
    int* x = (int*)malloc(sizeof(int));
    *x = 1;
    if ((err = pthread_create(&tid1, NULL, foo, (void*)x)) != 0) {
        printf("%s\n", strerror(err));
    }



    printf("leave main.\n");
}