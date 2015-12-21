#ifndef __MASTER_SERVER_INIT_H__
#define __MASTER_SERVER_INIT_H__

#include <sys/epoll.h>

#include "server_sockopt.h"

int master_server_init(int argc,char *argv[]);

void on_master_handle(struct sock_server *server,struct epoll_event events);


#endif