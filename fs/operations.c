#include "operations.h"
#include "config.h"
#include "state.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "betterassert.h"

tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}

int tfs_init(tfs_params const *params_ptr) {
    tfs_params params;
    if (params_ptr != NULL) {
        params = *params_ptr;
    } else {
        params = tfs_default_params();
    }

    if (state_init(params) != 0) {
        return -1;
    }

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    if (state_destroy() != 0) {
        return -1;
    }
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

/**
 * Looks for a file.
 *
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported.
 *
 * Input:
 *   - name: absolute path name
 *   - root_inode: the root directory inode
 * Returns the inumber of the file, -1 if unsuccessful.
 */
static int tfs_lookup(char const *name, inode_t const *root_inode) {
    // DONE: assert that root_inode is the root directory
    if (!valid_pathname(name)) {
        return -1;
    }
    inode_t *inode = inode_get(ROOT_DIR_INUM);
    if (inode != root_inode) { // checks if root_inode is the root directory
        return -1;
    }
    // skip the initial '/' character
    name++;
    return find_in_dir(root_inode, name);
}

int tfs_open(char const *name, tfs_file_mode_t mode) {
    // Checks if the path name is valid
    if (!valid_pathname(name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    ALWAYS_ASSERT(root_dir_inode != NULL,
                  "tfs_open: root dir inode must exist");
    int inum = tfs_lookup(name, root_dir_inode);
    size_t offset;

    if (inum >= 0) {
        // The file already exists
        inode_t *inode = inode_get(inum);
        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open: directory files must have an inode");
        if (inode->sym_link) {
            name = inode->sym_path; // get path of original file
            inum = tfs_lookup(name, root_dir_inode); // get inum of original file
            if (inum < 0) { // if original file doesn't exist
                return -1;
            }
            inode = inode_get(inum); // get inode of original file
        }
        // Truncate (if requested)
        if (mode & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                data_block_free(inode->i_data_block);
                inode->i_size = 0;
            }
        }
        // Determine initial offset
        if (mode & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (mode & TFS_O_CREAT) {
        // The file does not exist; the mode specified that it should be created
        // Create inode
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1; // no space in inode table
        }

        // Add entry in the root directory
        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            inode_delete(inum);
            return -1; // no space in directory
        }
        offset = 0;
    } else {
        return -1;
    }

    // Finally, add entry to the open file table and return the corresponding
    // handle
    return add_to_open_file_table(inum, offset);

    // Note: for simplification, if file was created with TFS_O_CREAT and there
    // is an error adding an entry to the open file table, the file is not
    // opened but it remains created
}

int tfs_sym_link(char const *target, char const *link_name) {
    if (!valid_pathname(target) || !valid_pathname(link_name)) {
        return -1;
    }
    // verify if target exists
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum < 0) { // target does not exist
        return -1;
    }
    inode_t *target_inode = inode_get(target_inum);
    pthread_rwlock_wrlock(&target_inode->rwlock);

    int link_inum;
    if (target_inode->sym_link) { // check if target is a symlink
        // if target is a symlink, create a new inode for the new symlink
        // and copy the original inode data to the new inode
        link_inum = inode_create(T_FILE);
        if (link_inum < 0) {
            return -1; // no space in inode table
        }
        inode_t *link_inode= inode_get(link_inum);
        link_inode->i_size = target_inode->i_size;
        link_inode->i_data_block = target_inode->i_data_block;
        link_inode->sym_link = true;
        link_inode->sym_path = target_inode->sym_path;
    } else {
        link_inum = tfs_lookup(link_name, root_dir_inode); 
        if (link_inum > 0) { // link already exists
            return -1;
        }
        link_inum = inode_create(T_FILE);
        if (link_inum < 0) {
            return -1;
        }
        inode_t *link_inode = inode_get(link_inum);
        pthread_rwlock_wrlock(&link_inode->rwlock);

        link_inode->sym_path = (char*)target;
        link_inode->sym_link = true;
    }
    if (add_dir_entry(root_dir_inode, link_name + 1, link_inum) == -1) {
        inode_delete(link_inum);
        return -1; // no space in directory
    }
    pthread_rwlock_unlock(&target_inode->rwlock);
    return 0; 
}

int tfs_link(char const *target, char const *link_name) {
    if (!valid_pathname(target) || !valid_pathname(link_name)) {
        return -1;
    }
    // check if target is symlink
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum < 0 ) { // target does not exist
        return -1;
    }
    inode_t *target_inode = inode_get(target_inum);
    pthread_rwlock_wrlock(&target_inode->rwlock);

    if (target_inode->sym_link == true) { // target is symlink
        pthread_rwlock_unlock(&target_inode->rwlock);
        return -1;
    }
    int link_inum = tfs_lookup(link_name, root_dir_inode); 
    if (link_inum > 0) { // check if link_name already exists
        pthread_rwlock_unlock(&target_inode->rwlock);
        return -1;
    }
    target_inode->hard_links++;
    
    // link entry points to target_inum
    if (add_dir_entry(root_dir_inode, link_name +1, target_inum) == -1) {
        inode_delete(link_inum);
        pthread_rwlock_unlock(&target_inode->rwlock);
        return -1; // no space in directory
    }
    pthread_rwlock_unlock(&target_inode->rwlock);
    return 0;
}

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1; // invalid fd
    }

    remove_from_open_file_table(fhandle);

    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) { 
        return -1;
    }

    //  From the open file table entry, we get the inode
    inode_t *inode = inode_get(file->of_inumber);
    pthread_rwlock_wrlock(&inode->rwlock);
    ALWAYS_ASSERT(inode != NULL, "tfs_write: inode of open file deleted");

    // Determine how many bytes to write
    size_t block_size = state_block_size();
    if (to_write + file->of_offset > block_size) {
        to_write = block_size - file->of_offset;
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {
            // If empty file, allocate new block
            int bnum = data_block_alloc();
            if (bnum == -1) {
                pthread_rwlock_unlock(&inode->rwlock);
                return -1; // no space
            }

            inode->i_data_block = bnum;
        }

        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_write: data block deleted mid-write");

        // Perform the actual write
        memcpy(block + file->of_offset, buffer, to_write);

        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
    }
    pthread_rwlock_unlock(&inode->rwlock);
    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    // From the open file table entry, we get the inode
    inode_t const *inode = inode_get(file->of_inumber);
    pthread_rwlock_rdlock((pthread_rwlock_t*)&inode->rwlock);
    ALWAYS_ASSERT(inode != NULL, "tfs_read: inode of open file deleted");

    // Determine how many bytes to read
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_read: data block deleted mid-read");

        // Perform the actual read
        memcpy(buffer, block + file->of_offset, to_read);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_read;
    }
    pthread_rwlock_unlock((pthread_rwlock_t*)&inode->rwlock);
    return (ssize_t)to_read;
}

int tfs_unlink(char const *target) {
    if (!valid_pathname(target)) {
        return -1;
    }
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum < 0) { // target does not exist
        pthread_rwlock_unlock(&root_dir_inode->rwlock);
        return -1;
    }

    inode_t *target_inode = inode_get(target_inum);
    pthread_rwlock_wrlock(&target_inode->rwlock);

    if (target_inode->sym_link == true) { // target is symlink
        clear_dir_entry(root_dir_inode, target + 1);
        inode_delete(target_inum);
        return 0;
    }

    if (target_inode->hard_links > 1) { // target has hard links
        target_inode->hard_links--;
        clear_dir_entry(root_dir_inode, target + 1);
        if (target_inode->hard_links == 0) {
            inode_delete(target_inum);
        }
        pthread_rwlock_unlock(&target_inode->rwlock);
        return 0;
    }
    // target has no hard links
    if (clear_dir_entry(root_dir_inode, target + 1) == -1) {
        pthread_rwlock_unlock(&target_inode->rwlock);
        return -1;
    }
    inode_delete(target_inum);
    return 0;
}

/*
 * Copy the contents of a file that exists in the OS's file system tree
 * (outside of the TFS) into a file in the TFS.
 *
 * Input:
 *   - source_path: path name of the source file (from the OS' file system)
 *   - dest_path: absolute path name of the destination file (in TécnicoFS),
 *   wich is created if needed, and overwritten if it already exists.
 *
 *   Return: 0 on success, -1 on error.
 */
int tfs_copy_from_external_fs(char const *source_path, char const *dest_path) {
    if (valid_pathname(dest_path) == 0) { // check if dest_path is valid
        printf("\n1\n");
        return -1;
    }
    FILE *fd = fopen(source_path, "r");
    if (fd == NULL) {
        printf("\n2\n");
        return -1;
    }
    char buffer[tfs_default_params().block_size]; // all files are at most 1 block by definition
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), fd);
    if (bytes_read != sizeof(buffer) && !feof(fd)) { // !feof is true if eof is reached
        printf("\n3\n");
        return -1;
    }
    int close_file = fclose(fd);
    if (close_file == -1) {
        printf("\n4\n");
        return -1;
    }
    int dest_fd = tfs_open(dest_path, TFS_O_CREAT);
    if (dest_fd == -1) {
        printf("\n5\n");
        return -1;
    }   
    ssize_t bytes_written = tfs_write(dest_fd, buffer, (size_t)bytes_read);
    if (bytes_written ==-1) {
        printf("\n6\n");
        return -1;
    }
    int close_dest_file = tfs_close(dest_fd);
    if (close_dest_file == -1) {
        printf("\n7\n");
        return -1;
    }
    return 0;
}

int tfs_list(){
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    pthread_rwlock_rdlock(&root_dir_inode->rwlock);
    for (int i = 0; i < root_dir_inode->i_size; i++) {
        root_dir_inode->i_data_block += i;
        dir_entry_t *entry = (dir_entry_t *)root_dir_inode;
        if (entry->d_inumber != 0) {
            inode_t *inode = inode_get(entry->d_inumber);
            pthread_rwlock_rdlock(&inode->rwlock);
            printf("%s", entry->d_name);
    }
    }
    pthread_rwlock_unlock(&root_dir_inode->rwlock);
    return 0;
}