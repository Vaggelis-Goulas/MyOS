#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_MAX_FILES 64
#define FS_MAX_PATH 128
#define FS_SECTOR_SIZE 512
#define FS_MAX_OPEN_FILES 32

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

typedef struct {
    char name[13];
    uint32_t size;
    uint32_t start_cluster;
    int is_dir;
} fs_file_t;

typedef struct {
    fs_file_t files[FS_MAX_FILES];
    int file_count;
} fs_dir_t;

typedef struct {
    int dir_entry;
    uint32_t position;
    uint32_t size;
    uint16_t start_cluster;
    uint16_t current_cluster;
    uint32_t cluster_offset;
    int flags;
    int ref_count;
    int in_use;
} fcb_t;

void fs_init();
int fs_open(const char *path, int flags);
int fs_close(int fd);
int fs_read(int fd, void *buffer, uint32_t size);
int fs_write(int fd, const void *buffer, uint32_t size);
int fs_write_at(int fd, const void *buffer, uint32_t size, uint32_t offset);
int fs_append(int fd, const void *buffer, uint32_t size);
int fs_seek(int fd, int offset, int whence);
uint32_t fs_tell(int fd);
uint32_t fs_size(int fd);
void fs_list();
void fs_cat(const char *path);
int fs_create(const char *path);
int fs_delete(const char *path);

int fs_fd_is_valid(int fd);
int fs_fd_to_fcb(int fd);
void fs_std_stream_init();

#define FS_O_RDONLY  0x0001
#define FS_O_WRONLY  0x0002
#define FS_O_RDWR    0x0003
#define FS_O_CREAT   0x0100
#define FS_O_TRUNC   0x0200
#define FS_O_APPEND  0x0400

#define FS_SEEK_SET 0
#define FS_SEEK_CUR 1
#define FS_SEEK_END 2

#endif