#ifndef FS_H_INCLUDED_
#define FS_H_INCLUDED_

#ifndef size_t
typedef  unsigned int size_t;
#endif

typedef struct inode_ {
    int mode;
    int block_id[8];
    unsigned int size;
    unsigned int dcnt;
    int next_id;
    int ref_count;
} inode;

typedef struct fs_ fs;
typedef struct fs_dir_ fs_dir;

enum FS_FMODE {
    FS_READ = 1,
    FS_WRITE = 2,
    FS_APPEND = 4,
    FS_EXSIT = 8
};

fs * fs_creatfs(const char * fname, int block_num, int inode_num);
fs * fs_openfs(const char* fname);
void fs_closefs(fs*);
int fs_errno(fs*);
void fs_pwd(fs*, char * buf, size_t buf_len);
int fs_chdir(fs*, const char* dir);
int fs_open(fs*, const char* fname, int mode);
void fs_close(fs*, int fd);
int fs_read(fs*, int fd, void* buf, size_t size);
int fs_write(fs*, int fd, void* buf, size_t size);
int fs_seek(fs*, int fd, int offset, int mode);
unsigned int fs_tell(fs*, int fd);
int fs_eof(fs*, int fd);
int fs_fstat(fs*, int fd, inode* inode);
int fs_remove(fs*, const char* path);
int fs_mkdir(fs*, const char* path);
int fs_removedir(fs*, const char* dir);
fs_dir * fs_opendir(fs*, const char* dir);
int fs_nextent(fs_dir*, char* buf, size_t buf_len);
void fs_closedir(fs_dir*);
int fs_link(fs*, const char* src, const char* dst);

#endif
