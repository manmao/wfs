#ifndef __SOCKET_PKG_H
#define __SOCKET_PKG_H

#include <stdbool.h>  //C99 标准包含的头文件 包括bool
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>


#include "message.h"

//版本号
#define VERSION    0x01

//数据包类型
#define TYPE_CMD   0X01
#define TYPE_DATA  0x02

//编码类型
#define CODE_LZMA  0x01
#define CODE_ZLIB  0x02
#define CODE_GZIP  0x03

typedef struct socket_pkg{
	uint8_t            version;      //协议版本
	uint8_t            type;         //传输数据类型  命令/数据(0x01/0x02)
	uint8_t	           code;         //数据编码方式  lmza/zlib(0x01/0x02)
	uint8_t			   bak;		     //bak
	uint32_t           data_len;     //数据长度
	uint16_t           checksum;     //数据校验和
	uint16_t           pkt_id;       //数据包的编号(一个时间戳值)
    unsigned  long     cmd;          //命令
    struct message     *msg;		 //msg
}socket_pkg_t;


#endif