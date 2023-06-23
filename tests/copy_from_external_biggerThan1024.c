#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_SIZE 1024

/*
 * This test case is to test the copy_from_external function with a file larger than the default tfs block size.
 * It will attempt to copy a file of size > tfs BLOCK_SIZE from the external file system to the tfs file system.
 * Since tfs only allows files of size <= BLOCK_SIZE, the copy should fail, if successful, then the size of the 
 * file in the tfs file system should be the same as the BLOCK_SIZE.
 * */

int main() {

    char *str_ext_file_1 = "Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.Teste de copia de arquivo externo para o nosso sistema de arquivos.TTem de Parar aqui"; 
    char *path_copied_file_1 = "/f1";
    char *path_src_1 = "tests/ficheiro_a_copiar_teste.txt";
    
    char buffer[BLOCK_SIZE];

    assert(tfs_init(NULL) != -1);

    int f;
    ssize_t r;

    f = tfs_copy_from_external_fs(path_src_1, path_copied_file_1);
    assert(f != -1);

    f = tfs_open(path_copied_file_1, TFS_O_CREAT);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r != -1 && r <= BLOCK_SIZE); 
    assert(!memcmp(buffer, str_ext_file_1, (size_t)r)); // compare the content of the file with the content of the buffer
    assert(strlen(str_ext_file_1) == r); // the file should have the same size as the block size
    
    r = tfs_close(f);
    assert(r != -1);

    printf("Successful test.\n");
    
    return 0;
}
