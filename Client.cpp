#include <cstdlib>
#include <cstring>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc,char *argv[]){
    char buf[BUFSIZ];

    int cfd = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET,"127.0.0.1",&server_addr.sin_addr.s_addr);
    int ret= connect(cfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(ret==-1){
        perror("connet error");
        exit(1);
    }
    int flag=fcntl(STDERR_FILENO,F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(STDIN_FILENO,F_SETFL,flag);
    flag=fcntl(cfd,F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd,F_SETFD,flag);

    while(1){
        ret=read(STDIN_FILENO,buf,sizeof(buf));
        if(ret>0){
            write(cfd,buf,ret);
        }
        if(!strncmp(buf,"exit",4)){
            strcpy(buf,"密码错误，请重新输入密码：");
            write(cfd,buf,strlen(buf));
            break;
        }
        int ret=read(cfd,buf,sizeof(buf));
        if(ret>0){
            write(STDOUT_FILENO,buf,ret);
        }
    }
    close(cfd);
    return 0;
}