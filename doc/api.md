# api

``` c++
  //bits :
  //D L
  
  struct inode {
      time_t create_time;
      time_t modify_time;
      int mode;
      int block_id[8];
      unsigned int size;
      int next_id;
      int ref_count;
  };
  
  struct superblock {
      char magic_number[4];
      uint_64 inode_offset;
      int free_blocks[100];
      int free_inode;
  };
  
  struct fs;
  
  fs * fs_creatfs(const char * fname, int size, int inode_num = -1);
  fs * fs_openfs(const char* fname);
  void fs_closefs(fs*);
  int& fs_errno(fs*);
  
  int fs_pwd(fs*, char * buf, size_t buf_len);
  int fs_chdir(fs*, const char*);
  enum FS_FMODE {
      FS_READ = 1,
      FS_WRITE = 2,
      FS_APPEND = 4,
      FS_EXSIT = 8
  };
  int fs_open(fs*, const char*, int mode);
  void fs_close(fs*, int)
  int fs_read(fs*, int fd, void* buf, size_t size)
  int fs_write(fs*, int fd, void* buf, size_t size)
  int fs_seek(fs*, int fd, int offset, int mode)
  unsigned int fs_tell(fs*, int)
  int fs_eof(fs*, int fd)
  int fs_fstat(fs*, int, inode*)
  int fs_remove(fs*, const char*)
  int fs_removedir(fs*, const char*)
  struct fs_dir;
  fs_dir * fs_opendir(fs*, const char*)
  int fs_nextent(fs_dir*, char* buf, size_t buf_len)
  void fs_closedir(fs_dir*)
  int fs_link(fs*, const char* src, const char* dst)
  
  
  
```
