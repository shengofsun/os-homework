#include "fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_FD 256

static const char magic[] = "\0221\0124f";
#define blocksz 4096
#define inodect (blocksz/64)
#define FREE_BLOCK_NUM 500
#define MAX_PATH_LEN 252
#define MAX_FNAME_LEN 124

typedef struct superblock_ {
    char magic_number[4];
    int block_offset;
    int inode_cnt;
    int block_cnt;
    int total_free_block_num;
    int free_blocks[FREE_BLOCK_NUM];
    int free_inode;
} superblock;

typedef struct fdesc_ {
    int inodeid;
    int mode;
    int used;
    unsigned int offset;
} fdesc;

typedef struct buffer {
    int dirty; 
    int free;
    int bid;
    char d[blocksz];
} buffer;

struct fs_ {
    int errno_;
    superblock sb;
    inode * inodes;
    fdesc fds[MAX_FD];
    FILE * fp;
    buffer buf[16];
    char cdir[MAX_PATH_LEN * 2];
    int dno;
};

struct fs_dir_ {
    int fd;
    int cur_off;
};

typedef struct dentry {
    char fname[MAX_FNAME_LEN];
    int inode;
} dentry;

static int writeblk(fs * f, int bid) {
    fseek(f->fp, f->sb.block_offset + bid * blocksz, SEEK_SET);
    return fwrite(f->buf[bid].d, blocksz, 1, f->fp);
}

static int pwd_gen(fs * f, char* buf, size_t buf_len) {
    if (f->dno == 0) {
        buf[0] = '/';
        return 1;
    }
    
}

static buffer* openblk(fs * f, int bid) {
    int i;
    int k;
    for (i = 0; i < 16; ++i)
        if (f->buf[i].bid == bid)
            return &f->buf[i];
    k = -1;
    for (i = 0; i < 16; ++i)
        if (f->buf[i].free) {
            k = i;
            break;
        }
    if (f->buf[k].dirty) {
        if (!writeblk(f, k)) {
            return NULL;
        }
    }
    return &f->buf[k];
}

static int free_blk(fs * f, int bid) {
    ++ f->sb.total_free_block_num;
    if (f->sb.block_cnt == FREE_BLOCK_NUM) {
        buffer * b = openblk(f, bid);
        if (b == NULL) {
            return 0;
        }
        memcpy(&b->d, f->sb.free_blocks, sizeof(f->sb.free_blocks));
        b->dirty = 1;
    }
    else {
        f->sb.free_blocks[f->sb.block_cnt++] = bid;
    }
    return 1;
}

static fs * new_fs() {
    fs * f = malloc( sizeof (*f) );
    int i;
    if (f == NULL) return NULL;
    memset(f->fds, 0, sizeof(f->fds));
    f->errno_ = 0;
    for (i = 0; i < 16; ++i) {
        f->buf[i].dirty = f->buf[i].free = 0;
        f->buf[i].bid = -1;
    }
    f->dno = 1;
    f->cdir[1] = '/';
    f->cdir[2] = '\0';
    return f;
}

static void add_entry(fs * f, int to, const char* str, int id) {
    dentry ent;
    strcpy(ent.fname, str);
    ent.inode = id;
    writei(to, f->inodes[to].dcnt * sizeof(dentry), &ent, sizeof(ent));
    ++f->inodes[to].dcnt;
}

static int init_super_block(fs * f, int nblk, int ninode) {
    superblock * sb;
    int i;
    int ret;

    sb = &f->sb;
    
    // format super block:
    // 1. set_magic_number:
    memcpy(sb->magic_number, magic, sizeof(sb->magic_number));

    // 2. init inode
    sb->inode_cnt = ninode;
    memset(f->inodes, 0, sizeof(inode) * ninode);
    sb->free_inode = 1;
    for (i = 2; i < ninode; ++i)
        f->inodes[i-1].next_id = i;
    // init root dir:
    f->inodes[0].ref_count = 255;
    add_entry(f, 0, ".", 0);
    add_entry(f, 0, "..", 0);
        
    // 3. init blk
    ret = 1;
    sb->total_free_block_num = 0;
    sb->block_cnt = 0;
    sb->block_offset = blocksz + (ninode / inodect);
    for (i = 0; ret == 1 && i < nblk; ++i)
        ret = free_blk(f, i);
    return ret;
}

static void format_path(char *buf) {

}

static fs_dir* opendiri(int inode) {
}

static int findindir(fs *f, int inode, const char * entname) {
    fs_dir * dir = opendiri(inode);
    dentry ent;
    while (nextent(dir, &ent)) {
        if (strcmp(ent.fname, entname) == 0) {
            return ent.inode;
        }
    }
    return -1;
}

static int openi(fs* f, const char* path) {
}

fs * fs_creatfs(const char* fname, int block_num, int inode_num) {
    fs * f;
    int i;
    if (block_num <= 10) return NULL;
    if (fname == NULL) return NULL;
    if (inode_num == -1) inode_num = block_num / 10;
    if (inode_num <= 0) return NULL;

    inode_num = ((inode_num - 1) / inodect + 1) * inodect;

    f = new_fs();
    f->fp = fopen(fname, "w+b");
    if (f->fp == NULL) {
        free(f);
        return NULL;
    }

    f->inodes = malloc( sizeof(inode) * inode_num );
    if (f->inodes == NULL) {
        free(f);
        return NULL;
    }

    if (!init_super_block(f, block_num, inode_num)) {
        free(f->inodes);
        free(f);
        return NULL;
    }
    
    return f;
}

fs * fs_openfs(const char * fname) {
    fs * f = new_fs();
    int inode_num;
    f->fp = fopen(fname, "r+b");
    if (f->fp == NULL) {
        free(f);
        return NULL;
    }
    fseek(f->fp, 0, SEEK_SET);
    fread(&f->sb, sizeof(f->sb), 1, f->fp);
    if (strcmp(f->sb.magic_number, magic) != 0) {
        free(f);
        return NULL;
    }

    inode_num = f->sb.inode_cnt;
    f->inodes = malloc( sizeof(inode) * inode_num);
    if (f->inodes == NULL) {
        free(f);
        return NULL;
    }
    fseek(f->fp, blocksz, SEEK_SET);
    fread(f->inodes, sizeof(inode) * inode_num, 1, f->fp);
    return f;
}

void fs_closefs(fs *f) {
    int i = 0;
    for (i = 0; i < 16; ++i)
        if (f->buf[i].dirty)
            writeblk(f, i);
    fclose(f->fp);
    free(f->inodes);
    free(f);
}

int fs_errno(fs* f) {
    return f->errno_;
}

void fs_pwd(fs* f, char* buf, size_t buf_len) {
    int len = strlen(f->cdir);
    if (buf_len < len) len = buf_len;
    memcpy(buf, f->cdir, len);
}

int fs_chdir(fs* f, const char* dir) {
    char buf[MAX_PATH_LEN * 2];
    if (dir[0] == '/')
        strcpy(buf, dir);
    else
        sprintf("%s/%s", f->cdir, dir);
    format_path(buf);
    int dn = openi(f, buf);
    if (dn == -1 || ((f->inodes[dn].mode & 1) == 0)) return -1;
    f->dno = dn;
    strcpy(f->cdir, buf);    
}

int fs_open(fs* f, const char* fname, int mode) {
    int i;
    int k = -1;
    if ((mode & FS_WRITE) == 0)
        mode |= FS_EXSIT;
    for (i = 0; i < MAX_FD; ++i)
        if (f->fds[i].used == 0) {
            k = i;
            break;
        }

    return k;
}

void fs_close(fs* f, int fd) {
    if (fd < 0 || fd >= MAX_FD) {
        return ;
    }
    f->fds[fd].used = 0;
}

int fs_read(fs* f, int fd, void* buf, size_t size) {

}

int fs_write(fs* f, int fd, void* buf, size_t size) {

}

int fs_seek(fs* f, int fd, int offset, int mode) {

}

unsigned int fs_tell(fs* f, int fd) {

}

int fs_eof(fs* f, int fd) {

}

int fs_fstat(fs* f, int fd, inode* inode) {

}

int fs_remove(fs* f, const char* path) {

}

int fs_removedir(fs* f, const char* dir) {

}

fs_dir * fs_opendir(fs* f, const char* dir) {

}

int fs_nextent(fs_dir* dir, char* buf, size_t buf_len) {

}

void fs_closedir(fs_dir* dir) {
 
}

int fs_link(fs* f, const char* src, const char* dst) {

}

