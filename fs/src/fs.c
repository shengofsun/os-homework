#include "../include/fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_FD 256

static const char magic[] = "\0221\012";
#define blocksz 4096
#define inodect (blocksz/64)
#define FREE_BLOCK_NUM 500
#define MAX_PATH_LEN 252
#define MAX_FNAME_LEN 124
#define MAX_FILE_SIZE (8*blocksz/sizeof(int)*blocksz)

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
    fs * f;
    int inode;
    int cur_off;
};

typedef struct dentry {
    char fname[MAX_FNAME_LEN];
    int inode;
} dentry;

static int openi(fs*, const char*, int);

static int writeblk(fs * f, int bid) {
    int ret;
    fseek(f->fp, f->sb.block_offset + f->buf[bid].bid * blocksz, SEEK_SET);
    ret = fwrite(f->buf[bid].d, blocksz, 1, f->fp);
//    printf("%d %d\n", bid, ret);
    return ret;
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
        f->buf[k].dirty = 0;
    }
    f->buf[k].bid = bid;
    fseek(f->fp, f->sb.block_offset + bid * blocksz, SEEK_SET);
    fread(f->buf[k].d, blocksz , 1, f->fp);
    return &f->buf[k];
}

static int alloc_blk(fs *f){
    int ret = -1;
    if (f->sb.total_free_block_num  == 0) return -1;
    -- f->sb.block_cnt;
    ret = f->sb.free_blocks[f->sb.block_cnt];
    if (f->sb.block_cnt == 0) {
        buffer *b = openblk(f, ret);
        if (b == NULL) {
            ++f->sb.block_cnt;
            return -1;
        }
        memcpy(f->sb.free_blocks, b->d, sizeof(f->sb.free_blocks));
        f->sb.block_cnt = FREE_BLOCK_NUM;
    }
    -- f->sb.total_free_block_num;
    return ret;
}

static int free_blk(fs * f, int bid) {
    if (f->sb.block_cnt == FREE_BLOCK_NUM) {
        buffer * b = openblk(f, bid);
        if (b == NULL) {
            return 0;
        }
        memcpy(b->d, f->sb.free_blocks, sizeof(f->sb.free_blocks));
        b->dirty = 1;
        f->sb.block_cnt = 1;
        f->sb.free_blocks[0] = bid;
    }
    else {
        f->sb.free_blocks[f->sb.block_cnt++] = bid;
    }
    ++ f->sb.total_free_block_num;        
    return 1;
}

static void release_inode_blk(fs * f, int ino) {
    int i;
    inode * in;
    in= &f->inodes[ino];
    if (in->mode & 2) {
        for (i = 0; i < 8; ++i)
            if (in->block_id[i] > 0) {
                buffer * b = openblk(f, in->block_id[i]);
                b->free = 0;
                int * begin = (int*) b->d;
                int * end = (int*) (b->d + blocksz);
                for (; begin < end; ++begin)
                    if (*begin > 0)
                        free_blk(f, *begin);
                b->free = 1;
                free_blk(f, in->block_id[i]);
            }
    }
    else {
        for (i = 0; i < 8; ++i)
            if (in->block_id[i] > 0)
                free_blk(f, in->block_id[i]);
    }
}

static int creatfile(fs * f, const char * fname) {
    return openi(f, fname, 1);
}

static fs * new_fs() {
    fs * f = malloc( sizeof (*f) );
    int i;
    if (f == NULL) return NULL;
    memset(f->fds, 0, sizeof(f->fds));
    f->errno_ = 0;
    for (i = 0; i < 16; ++i) {
        f->buf[i].dirty = 0;
        f->buf[i].free = 1;
        f->buf[i].bid = -1;
    }
    f->dno = 1;
    f->cdir[0] = '/';
    f->cdir[1] = '\0';
    return f;
}

static int bmap(fs *f, int ip, int bn){
    const int ic = blocksz/sizeof(int);
    if (bn < 0 || bn >= ic*8) return -1;
    inode* inode = &f->inodes[ip];
    if (!(inode->mode&2) && bn >= 8) {
        buffer *bp = openblk(f, alloc_blk(f));
        memset(bp->d, 0, sizeof(bp->d));
        bp->dirty = 1;
        memcpy(bp->d, inode->block_id, sizeof(inode->block_id));
        memset(inode->block_id, 0, sizeof(inode->block_id));
        inode->block_id[0] = bp->bid;
        inode->mode |= 2;
    }
    if (!(inode->mode&2)) return inode->block_id[bn] ?
                              inode->block_id[bn] :
                              (inode->block_id[bn]=alloc_blk(f));
    
    buffer* bp = openblk(f, bn/ic);
    int *ptr = (int*)bp->d;
    if (!ptr[bn%ic]){
        bp->dirty = 1;
        ptr[bn%ic] = alloc_blk(f);
    }
    return ptr[bn%ic];
}


static int writei(fs *f, int ip, int off, const void* ptr, int size){
    if (size == 0) return 0;
    if (size < 0 || size + off >= MAX_FILE_SIZE) return -1;
    int ret = size;
    const char* src = ptr;
    if (off % blocksz){
        buffer* bp = openblk(f, bmap(f, ip, off/blocksz));
        bp->dirty = 1;
        int t = blocksz - off % blocksz;
        if (t > size) t = size;
        memcpy(bp->d+off % blocksz, src, t);
        size -= t;
        src += t;
        off += t;
    }

    while (size >= blocksz){
        buffer* bp = openblk(f, bmap(f, ip, off/blocksz));
        bp->dirty = 1;
        memcpy(bp->d, src, blocksz);

        off += blocksz;
        src += blocksz;
        size -= blocksz;
    }

    if (size){
        buffer* bp = openblk(f, bmap(f, ip, off/blocksz));
        bp->dirty = 1;
        memcpy(bp->d, src, size);
        off += size;
    }

    if (f->inodes[ip].size < off) f->inodes[ip].size = off;
    return ret;
}

static int readi(fs *f, int ip, int off, void* ptr, int size){
    if (size < 0) return -1;
    inode *inode = &f->inodes[ip];
    if (size > inode->size - off) size = inode->size - off;
    if (!size) return 0;

    int ret = size;
    char* dst = ptr;
    if (off % blocksz){
        buffer* bp = openblk(f, bmap(f, ip, off/blocksz));
        int t = blocksz - off % blocksz;
        if (t > size) t = size;
        memcpy(dst, bp->d+off % blocksz, t);
        size -= t;
        dst += t;
        off += t;
    }

    while (size >= blocksz){
        buffer* bp = openblk(f, bmap(f, ip, off/blocksz));
        memcpy(dst, bp->d, blocksz);

        off += blocksz;
        dst += blocksz;
        size -= blocksz;
    }

    if (size){
        buffer* bp = openblk(f, bmap(f, ip, off/blocksz));
        memcpy(dst, bp->d, size);
    }

    return ret;
}

static void add_entry(fs * f, int to, const char* str, int id) {
    dentry ent;
    strcpy(ent.fname, str);
    ent.inode = id;
    writei(f, to, f->inodes[to].dcnt * sizeof(dentry), &ent, sizeof(ent));
    ++f->inodes[to].dcnt;
}

static int backwards(const char* buf, int last, int cnt)
{
    if ( last<=0 ) return 0;
    if ( buf[last]=='/'){
        if ( cnt<=1 ) return last;
        else
            return backwards(buf, last-1, cnt-1);
    }
    return backwards(buf, last-1, cnt);
}
/*
 *Convert from the relative path to absolute path.
 * */
static void format_path(char *buf)
{
    int i=0, state=0, backs=0, last=0;
    
    for (; buf[i] && buf[i]!='\n'; )
    {
            switch ( buf[i] )
                {
                case '/':
                    if ( state==0 )  buf[last++]=buf[i++];
                    else if ( state == 1 ) i++;
                    else if ( state == 2 ){
                        last = backwards(buf, last-1, backs);
                        buf[last++]=buf[i++];
                        backs=0;
                    }
                    state = 1;
                    break;

                case '.':
                    if ( state == 0 ) buf[last++]=buf[i++];
                    else if ( state == 1 ){ state = 2; buf[last++]=buf[i++]; backs++; }
                    else if ( state==2 ) { buf[last++]=buf[i++]; backs++; }
                    break;
                default:
                    if ( state==2 ) backs=0;
                    state = 0;
                    buf[last++]=buf[i++];
                }
    }

    if ( state==2 )
        last = backwards(buf, last-1, backs);
    if (last==0) last++;
    buf[last]=0;
}

static int split_dir(char* path, char* p[])
{
    int off = 0;

    if ( *path==0 ) return off;
    
    p[off++]=path;
    while (*path) {
        if (*path=='/') {
            p[off++]=path+1;
            *path=0;
        }
        path++;
    }
    return off;
}

/*
 *open inode by the path. relative path is supported.
 *If the create_flag is 0, then -1 will be returned if the path not exist.
 *if the create_flag is 1, a new file will be created if the last level of the path not exist.
 *If the create_flag is 2, father's inode will be returned.
 * */
static int openi(fs* f, const char* path, int create_flag)
{
    dentry entry;
    char full_path[500];
    char* p[50];

    int count, this_inode = 0, offset, i=0, bytes_get, father_inode = 0;

    if (path[0] == '/')
        strcpy(full_path, path);
    else
        sprintf(full_path, "%s/%s", f->cdir, path);
    format_path(full_path);

    count = split_dir(full_path+1, p);
    if ( count<=0 ) return 0;
    
    while ( i<count )
    {
        offset = 0;
        while ( (bytes_get = readi(f, this_inode, offset, &entry, sizeof(entry))) ) {
            if ( strcmp(p[i], entry.fname)==0 ) break;
            offset += sizeof(entry);
        }
        if (bytes_get>0){
            father_inode = this_inode;
            this_inode = entry.inode;
            i++;
        }
        else break;
    }
    if ( i>=count ) {
        return (create_flag==2)?father_inode:this_inode;
    }
    if ( i+1>=count && create_flag ){
        if (create_flag == 2) return this_inode;
        i = f->sb.free_inode; 
        if ( i==0 ) return -1;
        else {
            f->sb.free_inode = f->inodes[i].next_id;
            add_entry(f, this_inode, p[count-1], i);
            return i;
        }
    }
    return -1;
}

static int init_super_block(fs * f, int nblk, int ninode) {
    superblock * sb;
    int i;
    int ret;

    sb = &f->sb;
    
    // format super block:
    // 1. set_magic_number:
    memcpy(sb->magic_number, magic, sizeof(sb->magic_number));

    // 2. init blk
    ret = 1;
    sb->total_free_block_num = 0;
    sb->block_cnt = 0;
    sb->block_offset = blocksz * (1 + ninode / inodect) ;
    for (i = 0; ret == 1 && i < nblk; ++i)
        ret = free_blk(f, i);

    // 3. init inode
    sb->inode_cnt = ninode;
    memset(f->inodes, 0, sizeof(inode) * ninode);
    sb->free_inode = 1;
    for (i = 2; i < ninode; ++i)
        f->inodes[i-1].next_id = i;
    
    // init root dir:
    f->inodes[0].mode |= 1;
    f->inodes[0].ref_count = 255;
    add_entry(f, 0, ".", 0);
    add_entry(f, 0, "..", 0);
        
    return ret;
}

static fs_dir* opendiri(fs *f,  int inode) {
    if (inode == -1) return 0;
    if (!(f->inodes[inode].mode&1)) return 0;
    fs_dir* ret = malloc(sizeof(fs_dir));
    ret->f = f;
    ret->cur_off = 0;
    ret->inode = inode;
    return ret;
}

static void remove_entry(fs* f, int father_inode, int son_inode)
{
    dentry entry;
    int offset = 0, bytes_get;
    while ( (bytes_get = readi(f, father_inode, offset, &entry, sizeof(entry)))>0 ){
        if ( entry.inode == son_inode )
            break;
        offset += sizeof(entry);
    }
    if ( bytes_get > 0 )
    {
        readi(f, father_inode, sizeof(entry)*(f->inodes[father_inode].dcnt-1), &entry, sizeof(entry) );
        writei(f, father_inode, offset, &entry, sizeof(entry) );
        f->inodes[father_inode].dcnt--;

        f->inodes[son_inode].next_id  = f->sb.free_inode;
        f->sb.free_inode = son_inode;
    }
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

    fseek(f->fp, 0, SEEK_SET);
    for (i = 0; i < f->sb.block_offset + block_num * blocksz; i+=blocksz) {
        if (!fwrite(f->buf[0].d, blocksz, 1, f->fp)) {
            free(f->inodes);
            free(f);
            return NULL;
        }
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
    fseek(f->fp, 0, SEEK_SET);
    fwrite(&f->sb, blocksz, 1, f->fp);
    fwrite(f->inodes, sizeof(inode)*f->sb.inode_cnt, 1, f->fp);
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
    buf[len]=0;
}

int fs_chdir(fs* f, const char* dir) {
    char buf[MAX_PATH_LEN * 2];
    if (dir[0] == '/')
        strcpy(buf, dir);
    else
        sprintf(buf, "%s/%s", f->cdir, dir);
    format_path(buf);
    int dn = openi(f, buf, 0);
    if (dn == -1 || ((f->inodes[dn].mode & 1) == 0)) return -1;
    f->dno = dn;
    strcpy(f->cdir, buf);
    return 1;
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
    if (k != -1) {
        f->fds[k].inodeid = openi(f, fname, 0);
        if (f->fds[k].inodeid == -1) {
            if (mode & FS_EXSIT) return -1;
        }
        else {
            if ((mode & FS_WRITE) && (mode & FS_APPEND) == 0) { // remove current file
                f->fds[k].inodeid = -1;
                fs_remove(f, fname);
            }
        }

        if (f->fds[k].inodeid == -1) {
            f->fds[k].inodeid = creatfile(f, fname);
            if (f->fds[k].inodeid == -1)
                return -1;
        }
        
        f->fds[k].used = 1;
        f->fds[k].mode = (mode & (FS_READ | FS_WRITE));
        f->fds[k].offset = (mode & FS_APPEND) ? f->inodes[f->fds[k].inodeid].size: 0;
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
    if (!f->fds[fd].used) return -1;
    return readi(f, f->fds[fd].inodeid, f->fds[fd].offset, buf, size);
}

int fs_write(fs* f, int fd, const void* buf, size_t size) {
    if (!f->fds[fd].used) return -1;
    return writei(f, f->fds[fd].inodeid, f->fds[fd].offset, buf, size);
}

int fs_seek(fs* f, int fd, int offset, int mode) {
    if (!f->fds[fd].used) return -1;
    int off = f->fds[fd].offset, size = f->inodes[f->fds[fd].inodeid].size;
    if (mode == FS_SET) off = offset;
    else if (mode == FS_END) off = size + offset - 1;
    else if (mode == FS_CUR) off += offset;
    else return -1;
    return off < 0 || off >= size ? -1 : 0;
}

unsigned int fs_tell(fs* f, int fd) {
    if (!f->fds[fd].used) return -1;
    return f->fds[fd].offset;
}

int fs_eof(fs* f, int fd) {
    if (!f->fds[fd].used) return -1;
    return f->fds[fd].offset == f->inodes[f->fds[fd].inodeid].size - 1;
}

int fs_fstat(fs* f, int fd, inode* inode) {
    if (!f->fds[fd].used) return -1;
    memcpy(inode, &f->inodes[f->fds[fd].inodeid], sizeof(*inode));
    return 0;
}

int fs_remove(fs* f, const char* path) {
    int ino;
    int fno;
    ino = openi(f, path, 0);
    if (ino == -1 || (f->inodes[ino].mode & 1) == 1) {
        return -1;
    }
    fno = openi(f, path, 2);
    if (fno == -1) return -1;
    release_inode_blk(f, ino);
    remove_entry(f, fno, ino);
    return 0;
}

/* static char* find_parent_and_son(char* path) */
/* { */
/*     int len = strlen(path); */
/*     if ( len < 0 ) return NULL; */
/*     if ( path[len -1 ]=='/' ) path[--len]=0; */
/*     while ( path[len]!='/' && len>=0 ) len--; */
/*     if ( len<0 ) return NULL; */

/*     path[len]=0; */
/*     return path+len+1; */
/* } */

int fs_mkdir(fs* f, const char* path)
{
    char full_path[500];
    
    int ino_parent, new_inode;
    
    sprintf(full_path, "%s/%s", f->cdir, path);
    format_path(full_path);

    if (openi(f, full_path, 0) != -1) return -1;
    if ((ino_parent = openi(f, full_path, 2)) == -1) return -1;
    if ((new_inode = openi(f, full_path, 1)) == -1) return -1;

    f->inodes[new_inode].mode |= 1;
    
    add_entry(f, new_inode, ".", new_inode);
    add_entry(f, new_inode, "..", ino_parent);

    return 1;
}

int fs_removedir(fs* f, const char* dir) {
    int ino;
    int fno;
    ino = openi(f, dir, 0);
    if (ino == -1 || 
        (f->inodes[ino].mode & 1) == 0 ||
        f->inodes[ino].dcnt > 0) {
        return -1;
    }
    fno = openi(f, dir, 2);
    if (fno == -1) return -1;
    release_inode_blk(f, ino);
    remove_entry(f, fno, ino);
    return 0;
}

fs_dir * fs_opendir(fs* f, const char* path) {
    /* fs_dir * dir = malloc(sizeof(fs_dir)); */
    /* dir->inode = openi(f, path, 0); */
    /* if (dir->inode == -1) { */
    /*     free(dir); */
    /*     return NULL; */
    /* } */
    /* dir->f = f; */
    /* dir->cur_off=0; */
    /* return dir; */
    return opendiri(f, openi(f, path, 0));
}

int fs_nextent(fs_dir* dir, char* buf, size_t buf_len) {
    fs * f = dir->f;
    dentry ent;
    int len;
    if (dir->cur_off >= f->inodes[dir->inode].dcnt * sizeof(dentry)) { 
        return 0;
    }
    readi(f, dir->inode, dir->cur_off, &ent, sizeof(ent));
    dir->cur_off += sizeof(ent);
    len = strlen(ent.fname);
    if (buf_len - 1 < len) len = buf_len - 1;
    memcpy(buf, ent.fname, len);
    buf[len] = '\0';
    return 1;
}

void fs_closedir(fs_dir* dir) {
    free(dir);
}
