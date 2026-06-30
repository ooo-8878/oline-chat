#ifndef _LIST_H_
#define _LIST_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

// 定义结构体来表示单链表
struct clientlist
{
	// 数据域--》存放所有连接成功的客户端信息
	int sock;			 // 客户端套接字
	char ip[20];		 // 客户端ip地址
	unsigned short port; // 客户端端口号
	int is_online;		 // 标志位0: 只是普通连接/短连接  1: 真正聊天在线

	// 指针域
	struct clientlist *next;
};

// 初始化头结点
struct clientlist *client_init(void);

// 创建新节点
struct clientlist *create_node(int sock, struct sockaddr_in clientaddr);

// 尾插
struct clientlist *list_tail(int newsock, struct sockaddr_in clientaddr, struct clientlist *head);

// 删除指定客户端节点
int list_delete(char ipbuf[20], unsigned short delport, struct clientlist *head);

#endif