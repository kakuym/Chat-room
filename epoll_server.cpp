#include <arpa/inet.h>
#include <ctime>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>//提供了网络地址和套接字操作的更一般性的定义和功能，适用于IPv4和IPv6。
// #include <arpa/inet.h>//专注于IPv4地址的字符串和网络地址之间的转换
#include <sys/epoll.h>
#include <unistd.h>

#define MAX_EVENTS 1024
#define Port 8080

void login_menu(int cfd,void *arg);
typedef struct user{
    char user_id[8];
    char user_name[256];
    char user_key[40];
    int st;
}user_msg;

typedef void (*call_back)(int,void*);
typedef struct myevent_s{
    int fd;
    int events;
    call_back fun;
    void *arg;
    int status;
    char buf[BUFSIZ];
    int len;
    long long last_active_time;
    user_msg um;
    int log_step;
}myevent_s;
int g_efd;
myevent_s g_events[MAX_EVENTS+1];

char ms1[]="与服务器建立连接，开始进行数据通信 ------ [OK]\n"
           "           epoll服务器聊天室    \n"
           "  (1)匿名聊天   (2)登录    (3)注册    \n"
           ">>>  ";

#define ONLINE_MAX 100000
int online_fd[ONLINE_MAX],l[ONLINE_MAX],r[ONLINE_MAX],fd_pos[ONLINE_MAX],idx,online_num;
void list_init(){
    r[0]=1,l[1]=0;
    idx=2;
    for(int i=3;i<ONLINE_MAX;++i){
        fd_pos[i]=-1;
    }
}
void list_push(int fd){
    fd_pos[fd]=idx;
    online_fd[idx]=fd;
    r[idx]=r[0],l[idx]=0;
    l[r[0]]=idx,r[0]=idx++;
    online_num++;
}
void list_del(int fd){
    int k=fd_pos[fd];
    l[r[k]]=l[k];
    r[l[k]]=r[k];
    online_num--;
}
user_msg Users[MAX_EVENTS];
int user_num;

void load_usermsg(){
    FILE *fp=fopen("./user_msg","r");
    if(fp==NULL){
        perror("load error");
    }
    while(!feof(fp)){
        user_num++;
        fscanf(fp,"%s %s %s",Users[user_num].user_id,Users[user_num].user_name,Users[user_num].user_key);
        Users[user_num].st=0;
    }
    // user_num--;
}

void event_set(myevent_s *ev,int fd,int events,call_back fun,void *arg3){
    ev->fd=fd;
    ev->events=events;
    ev->fun=fun;
    ev->arg=arg3;
}
void event_add(int epfd,myevent_s *ev){
    struct epoll_event tep;
    tep.data.ptr=ev;
    tep.events=ev->events;
    if(epoll_ctl(epfd,EPOLL_CTL_ADD,ev->fd,&tep)==-1){
        printf("fail: epoll_cal_add fd: %d, events is %d\n",ev->fd,ev->events);
    }
    else{
        ev->status=1;
        ev->last_active_time=time(NULL);
    }
}
void event_del(int epfd,myevent_s *ev){
    ev->status=0;
    epoll_ctl(epfd,EPOLL_CTL_DEL,ev->fd,NULL);
}
void close_cfd(int cfd,myevent_s *ev){
    char str[BUFSIZ];
    event_del(g_efd,ev);
    close(cfd);
    sprintf(str,"the client fd: %d is close\n",ev->fd);
    write(STDOUT_FILENO,str,strlen(str));
    return;
}
void cb_accept(int lfd,void*arg){
    struct sockaddr_in client_addr;
    socklen_t client_addr_len =sizeof(client_addr);
    int cfd=accept(lfd,(struct sockaddr*)&client_addr,&client_addr_len);
    if(cfd==-1){
        perror("accept error");
        exit(1);
    }
    int i=0;
    for(i=0;i<MAX_EVENTS&&g_events[i].status!=0;++i);
    if(i==MAX_EVENTS){
        printf("the client num is max\n");
        return;
    }
    struct myevent_s *ev = &g_events[i];
    int flag=fcntl(cfd,F_GETFL);
    flag  |= O_NONBLOCK;
    fcntl(cfd,F_SETFL,flag);
    event_set(ev,cfd,EPOLLIN | EPOLLET,login_menu,ev);
    event_add(g_efd,ev);
    printf("the new client ip is %s, the client port is %d\n",
        inet_ntop(AF_INET,&client_addr.sin_addr.s_addr,ev->buf,sizeof(ev->buf)),ntohs(client_addr.sin_port));
    write(cfd,ms1,sizeof(ms1));
}

void cb_write(int cfd,void *arg);

void logout(int cfd,void *arg){
    myevent_s *ev=(myevent_s*)arg;
    char str[1024];
    list_del(cfd);
    ev->log_step=0;
    Users[atoi(ev->um.user_id)].st=0;
    sprintf(str,"已退出聊天室，当前在线人数为%d\n",online_num);
    sprintf(ev->buf,"(%s) %s\n>>>",ev->um.user_name,str);
    ev->len=strlen(ev->buf);
    cb_write(cfd,ev);
}
void cb_read(int cfd,void *arg){
    char str[BUFSIZ],str2[1024];
    myevent_s *ev=(myevent_s*) arg;
    int ret=read(cfd,str,sizeof(str));
    if(ret <= 0){
        logout(cfd,ev);
        close_cfd(cfd,ev);
        return;
    }
    str[ret]='\0';
    sprintf(str2,"from client fd: %d receive data is :",cfd);
    if(ret>0){
        write(STDOUT_FILENO,str2,strlen(str2));
    }
    write(STDOUT_FILENO,str,ret);
    sprintf(ev->buf,"(%s):%s\n>>>",ev->um.user_name,str);
    ev->len=strlen(ev->buf);
    event_del(g_efd,ev);
    event_set(ev,cfd,EPOLLOUT,cb_write,ev);
    event_add(g_efd,ev);
}
void cb_write(int cfd,void *arg){
    char str[BUFSIZ];
    myevent_s *ev=(myevent_s*)arg;
    if(ev->len <= 0){
        logout(cfd,ev);
        close_cfd(cfd,ev);
        return;
    }
    for(int i=r[0];i!=1;i=r[i]){
        if(online_fd[i] == cfd){
            continue;
        }
        write(online_fd[i],ev->buf,ev->len);
    }
    if(ev->log_step == 3){
        write(cfd,"\n>>>",4);
    }
    event_del(g_efd,ev);
    event_set(ev,cfd,EPOLLIN | EPOLLET,cb_read,ev);
    event_add(g_efd,ev);
}
void login(int cfd,void *arg){
    myevent_s *ev=(myevent_s*) arg;
    char *buf=ev->buf;
    int ret=read(cfd,buf,BUFSIZ);
    if(ret<=0){
        close_cfd(cfd,ev);
        return;
    }
    buf[ret-1]='\0';
    if(1 == ev->log_step){
        int id=atoi(buf);
        strcpy(ev->um.user_id,buf);
        char s[100];
        if(id > user_num || id <= 0){
            sprintf(s,"!用户UID:%s 不存在\n请重新输入账号UID:",buf);
            write(cfd,s,strlen(s));
            return;
        }
        if(Users[id].st){
            sprintf(s,"!用户UID:%s 已登录\n请重新输入账号UID:",buf);
            write(cfd,s,strlen(s));
            return;
        }
        strcpy(buf,"请输入密码:");
        write(cfd,buf,strlen(buf));
    }
    else if(2 == ev->log_step){
        int id=atoi(ev->um.user_id);
        strcpy(ev->um.user_key,buf);
        if(!strcmp(buf,Users[id].user_key)){
            strcpy(ev->um.user_name,Users[id].user_name);
            list_push(cfd);
            Users[id].st=1;
            sprintf(buf,">  用户：%s  已登录，当前在线人数为 %d     \n\n>>>",ev->um.user_name,online_num);
            ev->len=strlen(buf);
            char s[]="---------epoll聊天室----------\n";
            write(cfd,s,sizeof(s));
            write(cfd,buf,ev->len);
            event_del(g_efd,ev);
            event_set(ev,cfd,EPOLLOUT,cb_write,ev);
            event_add(g_efd,ev);
        }
        else{
            strcpy(buf,"密码错误，请重新输入密码：");
            write(cfd,buf,strlen(buf));
            return;
        }
    }
    ev->log_step++;
}

void get_uid(myevent_s *ev){
    char str[10];
    user_msg *p = &ev->um;
    sprintf(str,"%05d",user_num+1);
    strcpy(ev->um.user_id,str);

    FILE *fp=fopen("./user_msg","a+");
    if(fp = NULL){
        write(ev->fd,"error\n",6);
        fprintf(stderr,"get_uid open file error\n");
    }
    fprintf(fp,"%s %s %s\n",p->user_id,p->user_name,p->user_key);
    fflush(fp);
}
void register_id(int cfd,void *arg){
    myevent_s *ev=(myevent_s*) arg;
    char *buf=ev->buf;
    int ret=read(cfd,buf,BUFSIZ);
    if(ret <= 0){
        close_cfd(cfd,ev);
        return;
    }
    buf[ret-1]='\0';
    if(4 == ev->log_step){
        strcpy(ev->um.user_name,buf);
        strcpy(buf,"请设定账号的密码：");
        write(cfd,buf,strlen(buf));
    }
    else if(5 == ev->log_step){
        strcpy(ev->um.user_key,buf);
        strcpy(buf,"请再次输入密码：");
        write(cfd,buf,strlen(buf));
    }
    else if(6 == ev->log_step){
        if(strcmp(ev->um.user_key,buf)){
            strcpy(buf,"两次密码输入不一致，请重新输入：");
            write(cfd,buf,strlen(buf));
            return;
        }
        get_uid(ev);
        sprintf(buf,"注册成功，你的账号uid： %s  用户名为%s,现在重新返回登录界面 \n\n",ev->um.user_id,ev->um.user_name);
        user_num++;
        strcpy(Users[user_num].user_id,ev->um.user_id);
        strcpy(Users[user_num].user_name,ev->um.user_name);
        strcpy(Users[user_num].user_key,ev->um.user_key);
        ev->log_step=0;
        write(cfd,buf,strlen(buf));
        write(cfd,ms1,sizeof(ms1));

        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLIN | EPOLLET,login_menu,ev);
        event_add(g_efd,ev);
        return;
    }
    ev->log_step++;
}
void login_menu(int cfd,void *arg){
    myevent_s *ev=(myevent_s*) arg;
    char *buf=ev->buf;
    int ret=read(cfd,buf,BUFSIZ);
    if(ret<=0){
        close_cfd(cfd,ev);
        return;
    }
    if(buf[0]=='1'){
        sprintf(ev->um.user_name,"匿名用户 %ld",time(NULL));
        strcpy(ev->um.user_id,"00000");
        ev->log_step=3;
        list_push(cfd);

        sprintf(buf,">  用户：%s  已登录，当前在线人数为 %d    \n\n>>>",ev->um.user_name,online_num);
        ev->len=strlen(buf);
        char s[]="--------epoll聊天室测试版-----------\n";
        write(cfd,s,sizeof(s));
        write(cfd,buf,ev->len);

        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLOUT,cb_write,ev);
        event_add(g_efd,ev);
    }
    else if(buf[0]=='2'){
        ev->log_step=1;
        strcpy(buf,"请输入登录的UID:");
        write(cfd,buf,strlen(buf));

        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLIN | EPOLLET,login,ev);
        event_add(g_efd,ev);
    }
    else{
        strcpy(buf,"注册账号\n###请输入注册的用户名（中文/英文,注意不要包含特殊符号）：");
        write(cfd,buf,strlen(buf));
        ev->log_step=4;

        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLIN | EPOLLET,register_id,ev);
        event_add(g_efd,ev);
    }
}

int main(int argc,char *argv[]){
    list_init();
    load_usermsg();
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd==-1){
        perror("socket error");
        exit(1);
        return -1;
    }
    int opt=1;
    if(setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))==-1){
        perror("setsockopt error");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(Port);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(lfd,(struct sockaddr*)&server_addr,sizeof(server_addr))==-1){
        perror("bind error");
        exit(1);
    }
    if(listen(lfd,128)==-1){
        perror("listen error");
        exit(1);
    }

    g_efd=epoll_create(MAX_EVENTS+1);
    if(g_efd==-1){
        perror("epoll_create error");
        exit(1);
        return -1;
    }
    for(int i=0;i<MAX_EVENTS;++i){
        g_events[i].status=0;
    }
    myevent_s *lev=&g_events[MAX_EVENTS];
    event_set(lev,lfd,EPOLLIN,cb_accept,lev);
    event_add(g_efd,lev);
    struct epoll_event eps[MAX_EVENTS+1];
    int check_active=0;

    char t[]="---------server start------------[OK]\n";
    write(STDOUT_FILENO,t,sizeof(t));
    
    while(1){
        long long now_time=time(NULL);
        for(int i=0;i<100;++i,check_active++){
            if(check_active==MAX_EVENTS){
                check_active=0;
            }
            if(g_events[check_active].status!=1){
                continue;
            }
            if(now_time-g_events[check_active].last_active_time>=60){
                char *buf=g_events[check_active].buf;
                sprintf(buf,"---------太长时间未操作，已与服务器断开连接---------\n");
                write(g_events[check_active].fd,buf,strlen(buf));
                if(g_events[check_active].log_step==3){
                    logout(g_events[check_active].fd,&g_events[check_active]);
                }
                close_cfd(g_events[check_active].fd,&g_events[check_active]);
            }
        }
        int num=epoll_wait(g_efd,eps,MAX_EVENTS+1,500);
        for(int i=0;i<num;++i){
            myevent_s *ev=(myevent_s*)eps[i].data.ptr;
            ev->fun(ev->fd,ev->arg);
        }
    }
    close(lfd);
    return 0;
}