#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#include "socket_pkg.h"
#include "config.h"
#include "log.h"

#include "server.h"
#include "server_dispatch.h"
#include "mq_receiver.h"

#include "msg_queue.h"

server_t *master_server;
mq_t *mq;

void handle_request(void *arg);


static
void handle_sig(int sig)
{
    printf("catch sig %d\n",sig);
    exit(-1);
}

static 
int handle_unknown(int event_fd)
{
  return 0;
}

static
int on_accept(int client_conn_fd,struct sockaddr clientaddr)
{
   //往红黑树中插入节点
    struct conn_type *type=(struct conn_type *)malloc(sizeof(struct conn_type));
    type->node=(struct conn_node *)malloc(sizeof(struct conn_node));
    type->node->conn_fd = client_conn_fd;
    type->node->epoll_fd = master_server->efd;
    type->node->clientaddr = clientaddr;
    conn_insert(&(master_server->conn_root),type);
    return 0;
}

static
int on_readable(struct conn_node *node)
{
    //将数据包加入任务队列
    //放入线程池
    threadpool_add_job(master_server->tpool,(void *)&handle_request,node);
    return 0;
}

//子进程中启动监听线程,用于不断的从MQ中读取数据
static
int handle_listenmq()
{
   //开启线程监听消息队列
   pthread_t receiver_tid;
   pthread_create(&receiver_tid,NULL,(void *)&msg_queue_receiver,mq);
   pthread_detach(receiver_tid);
   return 0;
}


void handle_request(void *arg){
   struct conn_node *node=(struct conn_node *)arg;
   socket_pkg_t *socket_pkt_ptr=NULL;  
   while(1)
   {
       socket_pkt_ptr=create_socket_pkg_instance();
       
       assert(socket_pkt_ptr != NULL);

       int buflen=recv(node->conn_fd,(void *)socket_pkt_ptr,sizeof(socket_pkg_t),0);

       if(buflen < 0)
       {
           //读取完成
           if(errno== EAGAIN || errno == EINTR){ 
               log_write(CONF.lf,LOG_INFO,"no data:file:%s,line :%d\n",__FILE__,__LINE__);
           }else{
               log_write(CONF.lf,LOG_INFO,"error:file:%s,line :%d\n",__FILE__,__LINE__);                            //error
           }
           destroy_socket_pkg_instance(socket_pkt_ptr);
           return ;
       }
       else if(buflen==0)           
       {
          //删除连接节点
          conn_delete(&master_server->conn_root,node);
          destroy_socket_pkg_instance(socket_pkt_ptr);
          return ;
       }
       else if(buflen>0)
       {
        //消息分发
        socket_pkt_ptr->fd=node->conn_fd;
        handle_socket_pkg(mq,socket_pkt_ptr);
        log_write(CONF.lf,LOG_INFO,"data len:%d ,data checksum:%d\n",socket_pkt_ptr->data_len, socket_pkt_ptr->checksum);
       }
   }
}

int server_init(int argc,char *argv[])
{
    

    mq=init_meesage_queue();

    //挂接服务器事件处理函数
    struct server_handler *handler=(struct server_handler *)malloc(sizeof(struct server_handler));
    //如果没有相关接口实现的，一定要赋值为空值
    handler->handle_readable=&on_readable;
    handler->handle_accept=&on_accept;
    handler->handle_unknown=&handle_unknown;
    handler->handle_sig=&handle_sig;
    handler->handle_listenmq=&handle_listenmq;
    
    init_server(&master_server,NET_CONF.ip,NET_CONF.port,handler);
    start_listen(master_server,8,10000); //启动服务器监听子进程

    return 0;
}
