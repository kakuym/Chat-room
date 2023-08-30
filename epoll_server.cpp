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
    char user_id[8];//用户ID 五位UID
    char user_name[256];//用户名
    char user_key[40];//用户密码
    int st;//是否在线0-----离线  1------在线
}user_msg;

typedef void (*call_back)(int,void*);
//描述监听的文件描述符的相关信息的结构体
typedef struct myevent_s{
    int fd;//监听的文件描述符
    int events;//对应坚挺的事件 EPOLLIN/EPOLLOUT
    call_back fun;//回调函数
    void *arg;//回调函数的参数
    int status;//是否在监听红黑树上，1---在，0-----不在
    char buf[BUFSIZ];//读写缓冲区
    int len;//本次从客户端读入缓冲区数据的长度
    long long last_active_time;//该文件描述符最后在监听红黑树上的活跃时间
    user_msg um;//用户登录的信息
    int log_step;//标记用户位于登录的操作 0--未登录 1--输入账号 2--输入密码 3--成功登录 4--注册用户名 5--输入注册的密码 6--再次输入密码验证
}myevent_s;
int g_efd;//监听红黑树的树根
myevent_s g_events[MAX_EVENTS+1];//用于保存每个文件描述符信息的结构体的数组

char ms1[]="与服务器建立连接，开始进行数据通信 ------ [OK]\n"
           "           epoll服务器聊天室    \n"
           "  (1)匿名聊天   (2)登录    (3)注册    \n"
           ">>>  ";
//数组模拟双链表写法，链表保存当前在线的用户
#define ONLINE_MAX 100000
int online_fd[ONLINE_MAX],l[ONLINE_MAX],r[ONLINE_MAX],fd_pos[ONLINE_MAX],idx,online_num;
//链表初始化
void list_init(){
    r[0]=1,l[1]=0;
    idx=2;
    for(int i=3;i<ONLINE_MAX;++i){
        fd_pos[i]=-1;
    }
}
//链表中插入一个在线用户
void list_push(int fd){
    fd_pos[fd]=idx;
    online_fd[idx]=fd;
    r[idx]=r[0],l[idx]=0;
    l[r[0]]=idx,r[0]=idx++;
    online_num++;//在线人数++
}
//从链表中删除一个用户的文件描述符，删除fd的文件描述符
void list_del(int fd){
    int k=fd_pos[fd];
    l[r[k]]=l[k];
    r[l[k]]=r[k];
    online_num--;//在线人数--
}
user_msg Users[MAX_EVENTS];//已注册的所用用户的信息
int user_num;//已注册用户信息的数量

//从文件加载已经注册过的用户信息
void load_usermsg(){
    FILE *fp=fopen("./user_msg","r");//打开保存已注册用户信息的文件
    if(fp==NULL){
        perror("load error");
    }
    while(!feof(fp)){
        user_num++;
        fscanf(fp,"%s %s %s",Users[user_num].user_id,Users[user_num].user_name,Users[user_num].user_key);
        Users[user_num].st=0;//初始化所有用户为离线状态
    }
    // user_num--;
}
//重新设置监听事件
void event_set(myevent_s *ev,int fd,int events,call_back fun,void *arg3){
    ev->fd=fd;
    ev->events=events;
    ev->fun=fun;
    ev->arg=arg3;
}
//添加监听事件到树上
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
//将事件从监听红黑树上删除
void event_del(int epfd,myevent_s *ev){
    ev->status=0;
    epoll_ctl(epfd,EPOLL_CTL_DEL,ev->fd,NULL);
}
//关闭与客户端通信的文件描述符
void close_cfd(int cfd,myevent_s *ev){
    char str[BUFSIZ];
    event_del(g_efd,ev);
    close(cfd);
    sprintf(str,"the client fd: %d is close\n",ev->fd);
    write(STDOUT_FILENO,str,strlen(str));
    return;
}
//监听新的客户端建立连接
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
    int flag=fcntl(cfd,F_GETFL);//设置为非阻塞状态
    flag  |= O_NONBLOCK;
    fcntl(cfd,F_SETFL,flag);
    event_set(ev,cfd,EPOLLIN | EPOLLET,login_menu,ev);//及案例新连接后，事件设定为登录界面程序
    event_add(g_efd,ev);
    printf("the new client ip is %s, the client port is %d\n",//服务器端打印客户端的地址信息
        inet_ntop(AF_INET,&client_addr.sin_addr.s_addr,ev->buf,sizeof(ev->buf)),ntohs(client_addr.sin_port));
    write(cfd,ms1,sizeof(ms1));
}

void cb_write(int cfd,void *arg);
//用户登出
void logout(int cfd,void *arg){
    myevent_s *ev=(myevent_s*)arg;
    char str[1024];
    list_del(cfd);//从在线列表中删除
    ev->log_step=0;//标记为登出
    Users[atoi(ev->um.user_id)].st=0;//用户信息中将其标记为离线状态
    sprintf(str,"已退出聊天室，当前在线人数为%d\n",online_num);
    sprintf(ev->buf,"(%s) %s\n>>>",ev->um.user_name,str);
    ev->len=strlen(ev->buf);
    cb_write(cfd,ev);//手动调用向其他用户发送xxx用户登出的信息
}
//服务器端读事件
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
    write(STDOUT_FILENO,str,ret);//将客户端发来的数据在服务器端进行打印
    sprintf(ev->buf,"(%s):%s\n>>>",ev->um.user_name,str);//格式化客户端发来的数据---数据处理
    ev->len=strlen(ev->buf);
    //此时服务器接受客户端发来的数据，然后发送给其他的在线用户，故此时事件重新设定为写事件
    event_del(g_efd,ev);
    event_set(ev,cfd,EPOLLOUT,cb_write,ev);
    event_add(g_efd,ev);
}
//向在线用户发送数据写事件
void cb_write(int cfd,void *arg){
    char str[BUFSIZ];
    myevent_s *ev=(myevent_s*)arg;
    if(ev->len <= 0){
        logout(cfd,ev);
        close_cfd(cfd,ev);
        return;
    }
    for(int i=r[0];i!=1;i=r[i]){//遍历当前的在线链表，向在线用户发送
        if(online_fd[i] == cfd){
            continue;//发送数据给服务器的客户端一方不需要发送
        }
        write(online_fd[i],ev->buf,ev->len);
    }
    if(ev->log_step == 3){
        write(cfd,"\n>>>",4);//界面优化
    }
    //执行完一次事件之后-->从树上摘下-->重新设定要监听事件-->重新挂上树监听
    event_del(g_efd,ev);
    event_set(ev,cfd,EPOLLIN | EPOLLET,cb_read,ev);
    event_add(g_efd,ev);
}
//用户登录
void login(int cfd,void *arg){
    myevent_s *ev=(myevent_s*) arg;
    char *buf=ev->buf;
    int ret=read(cfd,buf,BUFSIZ);
    if(ret<=0){
        close_cfd(cfd,ev);
        return;
    }
    buf[ret-1]='\0';
    if(1 == ev->log_step){//读取用户输入用户名
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
    else if(2 == ev->log_step){//输入用户密码
        int id=atoi(ev->um.user_id);
        strcpy(ev->um.user_key,buf);
        if(!strcmp(buf,Users[id].user_key)){
            strcpy(ev->um.user_name,Users[id].user_name);
            list_push(cfd);//将当前的cfd添加进在线列表中
            Users[id].st=1;
            sprintf(buf,">  用户：%s  已登录，当前在线人数为 %d     \n\n>>>",ev->um.user_name,online_num);
            ev->len=strlen(buf);
            char s[]="---------epoll聊天室----------\n";
            write(cfd,s,sizeof(s));
            write(cfd,buf,ev->len);
            //设定为写事件，向当前在线用户发送xxx用户已登录信息
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
//获取一个未注册的UID
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
    fprintf(fp,"%s %s %s\n",p->user_id,p->user_name,p->user_key);//将新注册的用户信息写入保存用户信息的文件中
    fflush(fp);//刷新缓冲区，将内容写入到文件中
}
//注册新账号
void register_id(int cfd,void *arg){
    myevent_s *ev=(myevent_s*) arg;
    char *buf=ev->buf;
    int ret=read(cfd,buf,BUFSIZ);
    if(ret <= 0){
        close_cfd(cfd,ev);
        return;
    }
    buf[ret-1]='\0';
    if(4 == ev->log_step){//输入注册的用户名
        strcpy(ev->um.user_name,buf);
        strcpy(buf,"请设定账号的密码：");
        write(cfd,buf,strlen(buf));
    }
    else if(5 == ev->log_step){//输入用户密码
        strcpy(ev->um.user_key,buf);
        strcpy(buf,"请再次输入密码：");
        write(cfd,buf,strlen(buf));
    }
    else if(6 == ev->log_step){//验证两次用户密码
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
        //注册完账号，重新返回登录界面的程序进行监听
        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLIN | EPOLLET,login_menu,ev);
        event_add(g_efd,ev);
        return;
    }
    ev->log_step++;
}
//登录界面
void login_menu(int cfd,void *arg){
    myevent_s *ev=(myevent_s*) arg;
    char *buf=ev->buf;
    int ret=read(cfd,buf,BUFSIZ);
    if(ret<=0){
        close_cfd(cfd,ev);
        return;
    }
    if(buf[0]=='1'){//匿名用户登录
        sprintf(ev->um.user_name,"匿名用户 %ld",time(NULL));//设置匿名登录的用户名
        strcpy(ev->um.user_id,"00000");//所有匿名用户的账号为00000
        //加入到聊天 回调然后监听
        ev->log_step=3;//表示为已登录状态
        list_push(cfd);//加入当前的在线链表

        sprintf(buf,">  用户：%s  已登录，当前在线人数为 %d    \n\n>>>",ev->um.user_name,online_num);
        ev->len=strlen(buf);
        char s[]="--------epoll聊天室测试版-----------\n";
        write(cfd,s,sizeof(s));
        write(cfd,buf,ev->len);
        //重新设定监听事件为写，写的内容为向当前在线用户发送XXX已登录的信息
        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLOUT,cb_write,ev);
        event_add(g_efd,ev);
    }
    else if(buf[0]=='2'){//账号UID登录
        ev->log_step=1;
        strcpy(buf,"请输入登录的UID:");
        write(cfd,buf,strlen(buf));
        //将事件设定为登录的回调
        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLIN | EPOLLET,login,ev);
        event_add(g_efd,ev);
    }
    else{//注册
        strcpy(buf,"注册账号\n###请输入注册的用户名（中文/英文,注意不要包含特殊符号）：");
        write(cfd,buf,strlen(buf));
        ev->log_step=4;//标记为进行注册状态的准备输入注册的用户名
        //将事件监听设置为注册的回调
        event_del(g_efd,ev);
        event_set(ev,cfd,EPOLLIN | EPOLLET,register_id,ev);
        event_add(g_efd,ev);
    }
}

int main(int argc,char *argv[]){
    list_init();//初始化链表
    load_usermsg();//加载已注册用户的信息
    int lfd=socket(AF_INET,SOCK_STREAM,0);//创建监听套接字
    if(lfd==-1){
        perror("socket error");
        exit(1);
        return -1;
    }
    //设置端口复用
    int opt=1;
    if(setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))==-1){
        perror("setsockopt error");
        exit(1);
    }
    //绑定客户端的地址ip和端口号，设置监听上限
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
    //创建epoll的监听树根
    g_efd=epoll_create(MAX_EVENTS+1);
    if(g_efd==-1){
        perror("epoll_create error");
        exit(1);
        return -1;
    }
    for(int i=0;i<MAX_EVENTS;++i){
        g_events[i].status=0;//将数组各个位置标记为空闲状态
    }
    myevent_s *lev=&g_events[MAX_EVENTS];
    //将lfd的监听事件设定为与客户端建立连接
    event_set(lev,lfd,EPOLLIN,cb_accept,lev);
    event_add(g_efd,lev);
    struct epoll_event eps[MAX_EVENTS+1];
    int check_active=0;

    char t[]="---------server start------------[OK]\n";
    write(STDOUT_FILENO,t,sizeof(t));
    
    while(1){
        //若客户端经过一段时间没有与服务器进行数据通信，主动关闭cfd
        long long now_time=time(NULL);
        for(int i=0;i<100;++i,check_active++){
            if(check_active==MAX_EVENTS){
                check_active=0;
            }
            if(g_events[check_active].status!=1){
                continue;
            }
            if(now_time-g_events[check_active].last_active_time>=120){
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