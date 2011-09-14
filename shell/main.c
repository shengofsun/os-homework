#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../fs/include/fs.h"

fs* filesys;
static char workdir[1000];

static void help(const char* entry)
{
    if ( strcmp(entry, "main")==0 )
    {
        printf("Simple fs. By our yying and hard working.\n");
        printf("Usage:\n");
        printf("1. fs [file name] \n");
        printf("This command open a file as an file system\n");
        printf("2. fs [options]\n");
        printf("This command create a new file system.\n");
        printf("options:\n");
        printf("-f|--format filename --size|-s count1 --inodes|-i count2");
        printf("the new file system's file name is filename.\nAnd the size is the cout1. inode size is count2\n");
    }
}

static void ls_this_dir(const char* dir_name, int hide)
{
    static char entry[1000];
    fs_dir* fd = fs_opendir(filesys, dir_name);

    if ( fd==NULL )
    {
        printf("Directory %s not exist\n", dir_name);
        return;
    }
    
    printf("Directory: %s\n", dir_name);
    
    while ( fs_nextent(fd, entry, 999) )
    {
        if ( hide==0 || entry[0]!='.' )
            printf("%s\n", entry);
    }

    fs_closedir(fd);
}

void ls(char* params[], int p_cnt)
{
    int hide = 1, i;
    int dir_cnt = 0;
    
    for (i=0; i<p_cnt; i++)
        if ( strcmp(params[i], "-a")==0 )
            hide = 0;

    for (i=0; i<p_cnt; i++)
    {
        if ( params[i][0]!='-' ){
            ls_this_dir(params[i], hide);
            dir_cnt ++;
        }
    }

    if ( dir_cnt==0 )
        ls_this_dir(workdir, hide);
}

void cd(char* params[], int len)
{
    if ( len!=1 ) help("cd");
    else {
        if ( fs_chdir(filesys, params[0]) == -1 )
            printf("error occured\n");
    }
    fs_pwd(filesys, workdir, 999);
}

void pwd(char* params[], int len)
{
    if (len!=0) help("pwd");
    else{
        printf("%s\n", workdir);
    }
}

void mkdir(char* params[], int len)
{
    if ( len!=1 ) help("mkdir");
    else if ( fs_mkdir(filesys, params[0])==-1 )
        printf("error occured.\n");
}

static void remove_entry(const char* path)
{
    static char entry[1000];
    
    int fd = fs_open(filesys, path, 0);
    inode ibuffer;
    fs_dir* fdir;
    
    if ( fd==-1 ){
        printf("error occured.\n");
        return;
    }
    
    if ( fs_fstat(filesys, fd, &ibuffer) == -1 ){
        printf("error occured.\n");
        fs_close(filesys, fd);
        return;
    }

    if ( (ibuffer.mode&1) == 0 )
        fs_remove(filesys, path);
    else {
        fs_close(filesys, fd);
        fs_chdir(filesys, path);
        fdir = fs_opendir(filesys, ".");
        while ( fs_nextent(fdir, entry, 999) )
        {
            if ( strcmp(entry, ".")!=0 && strcmp(entry, "..")!=0 )
            {
                remove_entry(entry);
            }
        }
        fs_closedir(fdir);
        fs_chdir(filesys, "..");
        fs_removedir(filesys, path);
    }
}

void rm(char* params[], int len)
{
    int i = 0;
    
    if ( len==0 ) { help("rm"); return; }
    for (; i<len; i++)
    {
        remove_entry(params[i]);
    }
}

static int sh_stat(const char* pathname, inode *ibuffer)
{
    int r;
    int fd = fs_open(filesys, pathname, 0);
    if ( fd==-1 ) return -1;

    r = fs_fstat(filesys, fd, ibuffer);
    fs_close(filesys, fd);
    return r;
}

static int get_new_path(char* newp, const char* src, const char* dst)
{
    int len1 = strlen(dst), len2, i;
    memcpy(newp, dst, len1);

    for (i=0; src[i]; i++)
        if ( src[i]=='/' )
            len2=i;
    memcpy(newp+len1, src+len2+1, i-len2-1);

    return len1+i-len2-1;
}

static int st_stat(const char* filename, inode* ibuffer)
{
    int r;
    int fd = fs_open(filesys, filename, 0);

    if ( fd==-1 ) return -1;
    r = fs_fstat(filesys, fd, ibuffer);
    fs_close(filesys, fd);
    return r;
}

void cp(char* params[], int len)
{
    int fd1, fd2, len2;
    inode ibuffer;

    char* newp;
    char* buf;
    if ( len!= 2 ) { help("cp");return;}
    
    if ( st_stat(params[0], &ibuffer)==-1 || (ibuffer.mode&1) ) return;
    if ( st_stat(params[1], &ibuffer )==-1 || (ibuffer.mode&1)==0 ) return;
    if ( (fd1 = fs_open(filesys, params[0], FS_READ))==-1 ) return; 
    
    newp = (char*)malloc(1000);
    get_new_path(newp, params[0], params[1]);

    if ( sh_stat(newp, &ibuffer)!=-1 ) goto error;
    if ( (fd2=fs_open(filesys, newp, FS_READ|FS_WRITE))==-1 ) goto error;

    buf = (char*)malloc(1000);
    while ( (len2=fs_read(filesys, fd1, buf, 1000))>0 )
        fs_write(filesys, fd2, buf, len);

    free(buf);
    fs_close(filesys, fd2);
error:
    fs_close(filesys, fd1);
    free(newp);
}

void get(char* params[], int len)
{
    FILE* fp_src;
    inode ibuffer;
    int fd2, len2;
    char* newp;
    
    if ( len!=2 ) { help("get"); return; }

    if ( st_stat(params[1], &ibuffer )==-1 || (ibuffer.mode&1)==0 ) return;
    
    fp_src =fopen(params[0], "rb");
    if ( fp_src==NULL ) return;

    newp = (char*)malloc(1000);
    get_new_path(newp, params[0], params[1]);

    if ( sh_stat(newp, &ibuffer)!=-1 ) goto error;
    if ( (fd2=fs_open(filesys, newp, FS_READ|FS_WRITE))==-1 ) goto error;

    char* buf = (char*)malloc(1000);
    while ( (len2=fread(buf, 1, 1000, fp_src))>0 )
        fs_write(filesys, fd2, buf, len);

    free(buf);
    fs_close(filesys, fd2);
error:
    fclose(fp_src);
    free(newp);
}

void put(char* params[], int len)
{
    FILE* fp_dst;
    inode ibuffer;
    int fd, len2;
    char* newp;
    
    if ( len!=2 ) { help("put"); return; }

    if ( st_stat(params[0], &ibuffer )==-1 || ibuffer.mode&1 ) return;
    
    if ( (fd=fs_open(filesys, params[0], FS_READ))==-1 ) return;
    
    newp = (char*)malloc(1000);
    get_new_path(newp, params[0], params[1]);

    if ( ( fp_dst=fopen(newp, "wb") )==NULL ) goto error;

    char* buf = (char*)malloc(1000);
    while ( (len2=fs_read(filesys, fd, buf, 1000))>0 )
        fwrite(buf, 1, len, fp_dst);

    free(buf);
    fclose(fp_dst);
error:
    fs_close(filesys, fd);
    free(newp);
}

const char* commands[]={"ls", "cd", "pwd", "mkdir", "rm", "cp", "get", "put", NULL};
typedef void (*function)(char* p[], int l);
function func[]={ls, cd, pwd, mkdir, rm, cp, get, put};

fs* create_file_system(int argc, char** argv)
{
   int i, n_blks=0, n_inodes=0;
   char* fname=NULL;
   
   for (i=1; i<argc; i++)
   {
       if ( (strcmp(argv[i], "--format")==0 || strcmp(argv[i], "-f")==0) && i+1<argc ){
           fname = argv[i+1];
           i++;
       }
       else if ( (strcmp(argv[i], "--size")==0 || strcmp(argv[i], "-s")==0) && i+1 < argc ){
           n_blks = atoi(argv[i+1]);
           i++;
       }
       else if ( (strcmp(argv[i], "--inodes")==0 || strcmp(argv[i], "-i")==0) && i+1 < argc ){
           n_inodes = atoi(argv[i+1]);
           i++;
       }
   }

   if ( !fname || n_blks<=0 || n_inodes<0 )
       return NULL;
   if ( n_inodes == 0 )
       n_inodes = -1;
   return fs_creatfs(fname, n_blks, n_inodes);
}

int split_cmdline(char* options[], char* line)
{
    int current = 0;
    options[current++] = line;
    
    while (*line && *line!='\n')
    {
        if ( *line ==' ')
        {
            if ( *(line+1) )
                options[current++]=line+1;
            *line=0;
        }
        line++;
    }
    if (*line=='\n') *line=0;
    return current;
}

void parse_cmd(char* line)
{
    static char* options[50];
    
    int n_len = split_cmdline(options, line);
    int i;
    for (i=0; commands[i]; i++){
        if ( strcmp(commands[i], options[0])==0 )
            return func[i](options+1, n_len - 1);
    }
    printf("sbsh:command not found.\n");
}

void main_loop()
{
    static char cmdline[1000];
    printf("Welcome to shabby shell.\n");

    fs_pwd(filesys, workdir, 999);
    
    while ( 1 )
    {
        printf("%s $ ", workdir);
        if (fgets(cmdline, 1000, stdin)==NULL)
            goto error;
        
        if ( strcmp(cmdline, "exit\n")==0 )
            break;
        parse_cmd(cmdline);
    }

error:
    fs_closefs(filesys);
}

int main(int argc, char** argv)
{
    switch ( argc )
    {
    case 1:
        break;
        
    case 2:
        filesys = fs_openfs(argv[1]);
        break;

    default:
        filesys = create_file_system(argc, argv);
    }

    if ( filesys == NULL )
        help("main");
    else {
        main_loop();
    }

    return 0;
}
