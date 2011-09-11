#include <string.h>
#include <stdio.h>
#include "../fs/include/fs.h"

fs* filesys;
char cmdline[1000];

static void ls_this_dir(const char* dir_name, int hidden)
{
    static char entry[1000];
    
    printf("%s\n", dir_name);
    fs_dir* fd = fs_opendir(filesys, dir_name);
    
    while ( fs_nextent(fd, entry, 999)!=-1 )
    {
        printf("%s\n", entry);
    }
    printf("\n");

    fs_closedir(fd);
}

void ls(char* params[], int p_cnt)
{
    int hidden = -1, i;
    char* dir==NULL;
    int dir_cnt = 0;
    
    for (i=0; i<p_cnt; i++)
        if ( strcmp(params[i], "-a")==0 )
            hidden = 1;

    for (i=0; i<p_cnt; i++)
    {
        if ( params[i][0]!='-' ){
            ls_this_dir(params[i]);
            dir_cnt ++;
        }
    }

    if ( dir_cnt==0 )
    {
        dir = (char*)malloc(1005);
        if ( !dir )
            printf("allocate memory error\n");
        else
        {
            fs_pwd(filesys, dir, 1000);
            ls_this_dir(dir);
            free(dir);
        }
    }
}

void cd(char* params[], int len)
{
    if ( len!=1 ) help("cd");
    else {
        if ( fs_chdir(filesys, params[0]) == -1 )
            printf("error occured\n");
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
    int fd = fs_open(filesys, path, 0);
    inode ibuffer;

    if ( fd==-1 ){
        printf("error occured.\n");
        return;
    }
    
    if ( fstat(filesys, fd, ibuffer) == -1 ){
        printf("error occured.\n");
        fs_close(filesys, fd);
        return;
    }

    if ( ibuffer.mode == 0 )
    {
        
    }
}

void rm(char* params[], int len)
{
    int dirp = 0;
    int i = 0;
    int fd;
    if ( len==0 ) { help("rm"); return; }
    for (; i<len; i++)
    {
        remove_entry(parmas[i]);
    }
}

const char* commands={"ls", "cd", "pwd", "mkdir", "rm", "scp"};

fs* create_file_system(fs* f, int argc, char** argv)
{
   fs* temp_fs;
   int i, n_blks=0, n_inodes=0;
   char* fname=NULL;
   
   for (i=1; i<argc; i++)
   {
       if ( (strcmp(argv[i], "--format")==0 || strcmp(argv[i], "-f")==0) && i+1<argc ){
           fname = argv[i+1];
           i++;
       }
       else if ( (strcmp(argv[i], "--size")==0 || strcmp(argv[i], "-s")==0) && i+1 < argc ){
           n_blks = atoi(argv[i+1])
       }
       else if ( (strcmp(argv[i], "--inodes")==0 || strcmp(argv[i], "-i")==0) && i+1 < argc ){
           n_indoes = atoi(argv[i+1]);
       }
   }

   if ( !fname || n_blks<=0 || n_inodes<=0 )
       return NULL;
   if ( n_inodes == 0 )
       n_inodes == -1;
   return create_file_system(temp_fs, n_blks, n_inodes);
}

int split_cmdline(char* options[], char* line)
{
    int current = 0;
    options[current++] = *line;
    
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

    return current;
}

void exec_cmd(char* options[], int len)
{
    
}

void parse_cmd(char* line)
{
    static char* options[50];
    
    int n_len = split_cmdline(options, cmdline);
    exec_cmd(options, n_len);
}

void main_loop()
{
    printf("Welcome to mini file system.\n");
    while ( 1 )
    {
        printf("$");
        if (gets(cmdline)==NULL)
            goto error;
        parse_cmd(cmdline);
    }

error:
    
}

int main(int argc, char** argv)
{
    switch ( argc )
    {
    case 1:
        print_help();
        break;
        
    case 2:
        filesys = fs_openfs(argv[1]);
        break;

    default:
        filesys = create_file_system(argc, argv);
    }

    if ( filesys == NULL )
        print_help();
    else {
        main_loop();
    }
}
