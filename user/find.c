#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

void find(char *path, char *filename, int exec_flag, char *exec_cmd[]);

int main(int argc, char *argv[])
{
    if(argc < 3){
        fprintf(2, "Usage: find <directory> <filename> [-exec cmd...]\n");
        exit(1);
    }
    
    int exec_flag = 0;
    char *exec_cmd[MAXARG];
    int exec_argc = 0;
    
    // Check for -exec option
    if(argc >= 4 && strcmp(argv[3], "-exec") == 0){
        exec_flag = 1;
        for(int i = 4; i < argc; i++){
            exec_cmd[exec_argc++] = argv[i];
        }
        exec_cmd[exec_argc] = 0; // null terminate
    }
    
    find(argv[1], argv[2], exec_flag, exec_cmd);
    exit(0);
}

void find(char *path, char *filename, int exec_flag, char *exec_cmd[])
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    
    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    
    if(st.type != T_DIR){
        fprintf(2, "find: %s is not a directory\n", path);
        close(fd);
        return;
    }
    
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0)
            continue;
            
        if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            continue;
        
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        
        int entry_fd = open(buf, O_RDONLY);
        if(entry_fd < 0){
            continue;
        }
        
        if(fstat(entry_fd, &st) < 0){
            close(entry_fd);
            continue;
        }
        
        close(entry_fd);
        
        // If filename matches
        if(strcmp(de.name, filename) == 0){
            if(exec_flag){
                // Execute command for this file
                int pid = fork();
                if(pid == 0){
                    // Child process
                    char *cmd_argv[MAXARG];
                    int i;
                    
                    // Copy exec command
                    for(i = 0; exec_cmd[i] != 0; i++){
                        cmd_argv[i] = exec_cmd[i];
                    }
                    
                    // Add filename as last argument
                    cmd_argv[i++] = buf;
                    cmd_argv[i] = 0;
                    
                    exec(cmd_argv[0], cmd_argv);
                    fprintf(2, "find: exec failed for %s\n", buf);
                    exit(1);
                } else {
                    // Parent process
                    wait(0);
                }
            } else {
                // Normal mode - just print filename
                printf("%s\n", buf);
            }
        }
        
        // Recurse into subdirectories
        if(st.type == T_DIR){
            find(buf, filename, exec_flag, exec_cmd);
        }
    }
    
    close(fd);
}
