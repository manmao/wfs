#ifndef _MQ_SENDER_H_
#define _MQ_SENDER_H_

#include "msg_queue.h"


void send_msg_mq(linked_list_queue_t *msg_queue,message_t *msg){
	push_msg_tail(msg_queue,msg); //插入消息队列
}

#endif