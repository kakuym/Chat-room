#include <iostream>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <map>

using namespace std;
//最大连接数
const int MAX_CONN = 1024;

//保存客户端信息
struct Client{
    int sockfd;
    string name;
};

int main() {
    //创建监听的socket
    int sockfd=socket(AF_INET,SOCK_STREAM,0);
    if(sockfd<0){
        perror("socket error");
        return -1;
    }
    //绑定本地ip和端口
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htonl(8080);
    int ret = bind(sockfd,(struct sockaddr*)&addr,sizeof(addr));
    if(sockfd < 0){
        printf("bind error\n");
        return -1;
    }
    //监听客户端
    ret = listen(sockfd,1024);
    if(ret<0){
        printf("listen error\n");
        return -1;
    }

    //创建epoll实例
    int epld=epoll_create1(0);
    if(epld<0){
        perror("epoll create error");
        return -1;
    }
    //将监听的socket加入epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    ret=epoll_ctl(epld,EPOLL_CTL_ADD,sockfd,&ev);
    if(ret<0){
        printf("epoll_ctl error\n");
        return -1;
    }
    //保存客户端信息
    map<int,Client> clients;
    //循环监听
    while(1){
        struct epoll_event evs[MAX_CONN];
        int n=epoll_wait(epld,evs,MAX_CONN,-1);//阻塞
        if(n<0){
            printf("epoll_wait error\n");
            break;
        }
        for(int i=0;i<n;i++){
            int fd=evs[i].data.fd;
            //如果是监听的fd收到消息，那么则表示有客户端进行连接了
            if(fd = sockfd){
                struct sockaddr_in client_addr;
                socklen_t client_addr_len=sizeof(client_addr);
                int client_sockfd=accept(sockfd,(struct sockaddr*)&client_addr,&client_addr_len);
                if(client_sockfd<0){
                    printf("accept error\n");
                    continue;
                }
                // 将客户端的socket加如epoll
                struct epoll_event ev_client;
                ev.events = EPOLLIN;//检测客户端有没有消息过来
                ev.data.fd = client_sockfd;
                ret = epoll_ctl(epld,EPOLL_CTL_ADD,client_sockfd,&ev_client);
                if(ret<0){
                    printf("epoll_ctl error\n");
                    break;
                }
                printf("%s正在连接...",client_addr.sin_addr.s_addr);
                //保存该客户端的信息
                Client client;
                client.sockfd = client_sockfd;
                client.name = "";
                clients[client_sockfd] = client;
            }
            else{//如果是客户端消息
                char buffer[1024];
                int n=read(fd,buffer,1024);
                if(n<0){
                    //TODO 处理错误
                    break;
                }
                else if(n==0){
                    // 客户端连接断开
                    close(fd);
                    epoll_ctl(epld,EPOLL_CTL_DEL,fd,0);
                    clients.erase(fd);
                }
                else{
                    string msg(buffer,n);
                    // 如果该客户端name为空，说明该消息是这个客户端的用户名
                    if(clients[fd].name==""){
                        clients[fd].name = msg;
                    }
                    else{//否则是聊天消息
                        string name = clients[fd].name;
                        //把消息发给其他客户端
                        for(auto &c : clients){
                            if(c.first != fd){
                                write(c.first,('['+name+']'+":"+msg).c_str(),msg.size()+name.size()+4);
                            }
                        }
                    }
                }
            }
        }
    }
    //关闭epoll实例
    close(epld);
    close(sockfd);
    return 0;
}
