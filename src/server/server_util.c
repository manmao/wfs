﻿#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <error.h>
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <log.h>

#include "server_util.h"

#include "util.h"
#include "config.h"
#include "error_hdr.h"

#include "topic_fd_map.h"


/*******************************************
	当客户端可服务器端建立连接时

	添加fd,到epoll内核事件表中

	efd为内核事件表的文件描述符
	fd为添加的fd 文件描述符

********************************************/
void addfd(int epollfd,int fd){
	struct epoll_event event;
	event.data.fd=fd;
	event.events=EPOLLIN | EPOLLET | EPOLLPRI;   //ET 模式,为fd注册事件
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event); //添加fd和事件
	setnonblock(fd);
}


/***********************
	客户端和服务器端端开连接

	efd为内核事件表的文件描述符
	fd为添加的fd 文件描述符

************************/

void deletefd(int epollfd,int fd){
	struct epoll_event event;
	event.data.fd=fd;
	event.events=EPOLLIN | EPOLLET |  EPOLLPRI;  //ET 模式
	epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,&event);  //删除fd和事件
	close(fd);
}

/*******************************

	修改fd 的epollfd属性

*********************************/
void modfd(int epollfd,int fd,int ev)
{
	struct epoll_event event;
	event.data.fd=fd;
	event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
	epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}


void set_nodelay(int fd){

	int optval; 	   //整形的选项值
	socklen_t optlen;  //整形的选项值的长度
	
	/*******禁用Nagle算法***********/
	optval=1;
	optlen=sizeof(optval);
	setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,optlen);
}

/********************************
*设置套接字属性
*	主要作用是：
*				开启keepalive属性
*				设置端口和地址可重用*
*				禁用Nagle算法
*  函数名:client_set_sock
*  函数参数：
*  		@param  client 客户端操作实体
*		@param  sfd    和服务器端连接的套接字文件描述符
* 函数返回:
*		@return  null
*********************************/

void  server_set_sock(int sfd){

	int optval; 	   //整形的选项值
	socklen_t optlen;  //整形的选项值的长度
	int err;		   //设置返回结果

    /***设置端口和地址可重用**/
	optval=1;
	optlen=sizeof(optval);
	err=setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,(char *)&optval,optlen);

	/*******禁用Nagle算法***********/
	optval=1;
	optlen=sizeof(optval);
	err=setsockopt(sfd,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,optlen);

    ///设置发送缓冲区大小
    int iSockBuf = 1024 * 1024;
    while (setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, (void*)&iSockBuf, sizeof(iSockBuf)) < 0)
    {
        iSockBuf -= 1024;
        if (iSockBuf <= 1024) break;
    }

    ///设置接收缓冲区大小
    iSockBuf = 1024 * 1024;
    while(setsockopt(sfd, SOL_SOCKET, SO_RCVBUF, (void *)&iSockBuf, sizeof(iSockBuf)) < 0)
    {
        iSockBuf -= 1024;
        if (iSockBuf <= 1024) break;
    }

    //设置linger,关闭close后的延迟，不进入TIME_WAIT状态
    //struct linger{#1:开关 0关 1开,#2:时间值，毫秒}
    //strcut linger设置socket close后是否进入WAIT状态
    struct linger ling= {0, 0};
    if (setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling))!= 0)
    {
        return ;
    }

    /*******设置文件描述符非阻塞*********/
	setnonblock(sfd);
}


/**********************************
	函数功能：处理epoll数据连接
	函数参数:
			@param  server ------- server_t参数

	函数返回：
			@return -------  void
**********************************/
static
void handle_accept_event(server_t *server)
{
	struct sockaddr clientaddr;
	socklen_t addrlen=sizeof(struct sockaddr);  //地址长度

    int conn_fd=-1;
    //读取客户端的连接
    while (1)
    {
	    conn_fd=accept(server->listenfd,(struct sockaddr *)&clientaddr,&(addrlen)); 
        if(conn_fd>0){
	        //添加到epoll监听队列
	        addfd(server->efd,conn_fd);
		    //回调函数调用
           if(server->handler->handle_accept){
            	server->handler->handle_accept(conn_fd,clientaddr);
       	   }
	    }else{
	         if(errno == EAGAIN){ break;}
	    }
        
     } //end while
	
     if (conn_fd == -1) {
       	  if (errno != EAGAIN && errno != ECONNABORTED
                  	 && errno != EPROTO && errno != EINTR){
            log_write(CONF.lf,LOG_ERROR,"error,file:%s,line:%d",__FILE__,__LINE__);
  		  }
     } //end if
}


static
void handle_readable_event(server_t *server,struct epoll_event event){
    int event_fd=event.data.fd;

    if(server->handler->handle_readable != NULL)
    {
        server->handler->handle_readable(event_fd);
    }
}


static
void handle_unknown_event(server_t *server,struct epoll_event event)
{
    int event_fd=event.data.fd;
    if(server->handler->handle_unknown)
         server->handler->handle_unknown(event_fd);
}

/**********************************************
	服务器监听器
	当有数据来 		启动handler线程处理
	当断开连接时	删除和释放内存空间
***********************************************/
static 
void op_server_listener(void *arg){

    server_t *server=(server_t *)arg;
	struct epoll_event events[MAXEVENTS]; //epoll最大事件数,容器
    
    while(true){
      	//被改变值时退出循环
		//等待内核通知，获取可读的fd
		int number=epoll_wait(server->efd,events,MAXEVENTS,-1);
		if(number < 0)
		{
			log_write(CONF.lf,LOG_ERROR,"epoll failure\n");
			break;
		}

    	int i;
		for(i=0;i<number;i++){                   //遍历epoll的所有事件
      		int sockfd=events[i].data.fd;          //获取fd
      		if(sockfd == server->listenfd){          //有客户端建立连接
				handle_accept_event(server);
			}else if(events[i].events & EPOLLIN){       //efd中有fd可读,
				handle_readable_event(server,events[i]);
			}else{                                      //其他
				handle_unknown_event(server,events[i]);
			}
		}
	}
	//pthread_exit(NULL); 线程退出返回值NULL
}



/**************************

     初始化服务器端口

***************************/
void  init_server(server_t *server,char *ip,int port,struct server_handler *handler){
  	
  	int sfd=socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in addr;
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	addr.sin_addr.s_addr=inet_addr(ip);

    server_set_sock(sfd);                 //服务器套接字文件描述符

	int ret=bind(sfd,(struct sockaddr *)&addr,sizeof(addr));  //绑定到服务器的端口
    if(ret == -1){
		log_write(CONF.lf,LOG_ERROR,"绑定到服务器的端口失败\n");
		close(sfd);
		return ;
	}

	ret=listen(sfd,LISTEN_BACKLOG);               //监听端口
	assert(ret != -1);

    int efd=epoll_create(MAXCONNS);        //创建epoll事件监听

    addfd(efd,sfd);         			   //注册TCP socket 上可读事件

    /**初始化客户端连接的数量**/
	server->listenfd=sfd;
	server->efd=efd;
	server->conn_root=RB_ROOT;
	server->handler=handler;

	//初始化hash表
    server->ht=create_fdtopic_hashtable();

    //初始化锁
    pthread_rwlock_init(&(server->ht_lock),NULL);
    pthread_rwlock_init(&(server->rb_root_lock),NULL);

	//初始化MQ群组
	int i=0;
    for(i=0;i<CONF.queue_num;i++){
       (server->mq)[i]=init_meesage_queue(&server->conn_root,server->ht,
       							&(server->ht_lock),
       							&(server->rb_root_lock)); //
    }
    server->queues=CONF.queue_num;

    /** 注册监听信号进程 **/
    if(handler->handle_sig){
        signal(SIGKILL,handler->handle_sig);
        signal(SIGINT,handler->handle_sig);
	    signal(SIGTERM,handler->handle_sig);
    }

	log_write(CONF.lf,LOG_INFO,"init server sucesss!\n");
}


//
static void child_process(server_t *server,int thread_num,int thread_queue_num){
   
   server->workqueue=(struct workqueue *)malloc(sizeof(struct workqueue));

   //初始化线程池
   if(thread_num ==0 || thread_queue_num == 0){
   	  workqueue_init(server->workqueue, DEFAULT_THREAD_NUM);//初始化线程池,默认配置
   }else{
   	  workqueue_init(server->workqueue, thread_num);//初始化线程池，用户配置
   }
   //开始监听
   op_server_listener(server);
}



/**
 * 那个进程监听
 * @param  server           [服务器对象]
 * @param  thread_num       [线程个数]
 * @param  thread_queue_num [线程池队列长度]
 * @return pid              [监听进程的pid]
 */
void start_listen(server_t *server,int thread_num,int thread_queue_num){
    
    assert(server!=NULL);
    //开启进程监听
    pid_t server_pid;

    server_pid=fork();
    if(server_pid <= -1)
    {
         log_write(CONF.lf,LOG_ERROR,"监听失败,file:%s,line:%d",__FILE__,__LINE__);
         errExit("fork失败,file:%s,line:%d",__FILE__,__LINE__);
    }
    if(server_pid == 0){ //子进程
         server->handler->handle_listenmq();  //针对每个队列启动线程
         child_process(server,thread_num,thread_queue_num);
    }else if(server_pid > 0){ //父进程
          int status;
	  pid_t ret;
          ret = wait(&status);   //wait
          if(ret <0){
		    perror("wait error");
		    exit(EXIT_FAILURE);
	  }

          //exit normal
	    if (WIFEXITED(status)){
		    log_write(CONF.lf,LOG_INFO,"child exited normal exit status=%d\n",WEXITSTATUS(status));
	    }

	    //exit signal
	    else if (WIFSIGNALED(status)){
		    log_write(CONF.lf,LOG_ERROR,
                    "****Sever Exception Exit!!!!**\n \
                    child exited abnormal signal number=%d\n file:%s,line:%d\n",
                    __FILE__,__LINE__,WTERMSIG(status));
	    }
	    //exit un normal
	    else if (WIFSTOPPED(status)){
		    log_write(CONF.lf,LOG_ERROR,"****Sever Exception Exit!!!!**** \
                child stoped signal number=%d \n", WSTOPSIG(status));
            //重新开启子进程
            child_process(server,thread_num,thread_queue_num);
        }

	}

}



/************************
*
*   关闭服务器器监听
*
*************************/
void destroy_server(server_t *server){

	/****删除所有连接节点****/
	deletefd(server->efd,server->listenfd);

	close(server->listenfd);
	close(server->efd);

	workqueue_shutdown(server->workqueue);

	free(server);
	log_write(CONF.lf,LOG_INFO,"服务器关闭\n");
}





