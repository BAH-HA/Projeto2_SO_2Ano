#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define BLOCK_SIZE 1024
#define NUM_THREADS 10
#define NUM_OPERATIONS 10

/*
 * This program creates NUM_THREADS threads, each of which performs
 * concurrent NUM_OPERATIONS reads on a file in the file system.
 * */

char *target_path = "/f1";
char *path_src = "tests/ficheiro_a_copiar_teste.txt";

void *thread_read_fn(void *arg);

int main() {
    pthread_t tid[NUM_THREADS];
                      
    assert(tfs_init(NULL) != -1);

    int f = tfs_open(target_path, TFS_O_CREAT);
    assert(f != -1);
    assert(tfs_close(f) != -1);

    f = tfs_copy_from_external_fs(path_src, target_path);
    assert(f != -1);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&tid[i], NULL, thread_read_fn, NULL);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(tid[i], NULL);
    }

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}


void *thread_read_fn(void *input) {
    (void)input; // ignore unused parameters
   
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        int fhandle = tfs_open(target_path, TFS_O_CREAT);
        assert(fhandle != -1);
        uint8_t buffer[BLOCK_SIZE];
        ssize_t r = tfs_read(fhandle, buffer, sizeof(buffer) - 1);
        assert(r != -1);
        assert(r == sizeof(buffer) - 1);
        // clear out buffer
        memset(buffer, 0, sizeof(buffer));
        tfs_close(fhandle);
    }
    return NULL;
}
