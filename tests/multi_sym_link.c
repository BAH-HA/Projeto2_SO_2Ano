#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This program tests if the tfs supports multiple symbolic links.
 * It creates a file, then creates sym links of sym links and tries
 * to read the file through the last sym link.
 * */

#define SYM_LINKS 20

uint8_t const file_contents[] = "AAA!";
char const target_path1[] = "/f1";
char const template_path[] = "/l%d";

void assert_contents_ok(char const *path) {
    int f = tfs_open(path, 0);
    assert(f != -1);

    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(memcmp(buffer, file_contents, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

void assert_empty_file(char const *path) {
    int f = tfs_open(path, 0);
    assert(f != -1);

    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

void write_contents(char const *path) {
    int f = tfs_open(path, 0);
    assert(f != -1);

    assert(tfs_write(f, file_contents, sizeof(file_contents)) ==
           sizeof(file_contents));

    assert(tfs_close(f) != -1);
}

int main() {
    assert(tfs_init(NULL) != -1);

    // Write to sym link of sym link of sym link of file and check that the
    // contents are correct.
    {
        int f = tfs_open(target_path1, TFS_O_CREAT);
        assert(f != -1);
        assert(tfs_close(f) != -1);

        assert_empty_file(target_path1); // sanity check
    }
    char *sym_link_paths[SYM_LINKS];

    // create paths for sym links
    for (int i = 0; i < SYM_LINKS; i++) {
        sym_link_paths[i] = malloc(10);
        sprintf(sym_link_paths[i], template_path, i);
    }
    
    // first sym link
    assert(tfs_sym_link(target_path1, sym_link_paths[0]) != -1);
    assert_empty_file(sym_link_paths[0]); // sanity check
    
    // create sym links of sym links
    for (int i = 1; i < SYM_LINKS; i++) {
        assert(tfs_sym_link(sym_link_paths[i-1], sym_link_paths[i]) != -1);
        assert_empty_file(sym_link_paths[i]);
    }

    write_contents(sym_link_paths[SYM_LINKS - 1]);
    assert_contents_ok(target_path1);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}
