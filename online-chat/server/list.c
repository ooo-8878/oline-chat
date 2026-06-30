#include "list.h"

//初始化
struct clientlist *client_init()
{
    struct clientlist *head = malloc(sizeof(struct clientlist));
    if(head == NULL)
    {
        perror("创建头结点失败\n");
        return NULL;
    }

    memset(head, 0, sizeof(struct clientlist));
    head->sock = -1;   // 头结点本身不代表真实客户端
    head->next = NULL;
    return head;
}


//创建新节点
struct clientlist *create_node(int newsock, struct sockaddr_in clientaddr)
{
    struct clientlist *newnode = malloc(sizeof(struct clientlist));
    if(newnode == NULL)
    {
        perror("创建节点失败\n");
        return NULL;
    }

    memset(newnode, 0, sizeof(struct clientlist));
    newnode->sock = newsock;
    strcpy(newnode->ip, inet_ntoa(clientaddr.sin_addr));
    newnode->port = ntohs(clientaddr.sin_port);
    newnode->is_online = 0;   // 默认先不是在线聊天用户
    newnode->next = NULL;

    return newnode;
}

//尾插
struct clientlist *list_tail(int newsock, struct sockaddr_in clientaddr, struct clientlist *head)
{
    if(head == NULL)
        return NULL;

    struct clientlist *newnode = create_node(newsock, clientaddr);
    if(newnode == NULL)
        return NULL;

    struct clientlist *p = head;
    while(p->next != NULL)
        p = p->next;

    p->next = newnode;

    return newnode;
}


//删除
int list_delete(char ipbuf[20], unsigned short delport, struct clientlist *head)
{
    if(head == NULL)
        return -1;

    struct clientlist *p = head->next;
    struct clientlist *q = head;

    while(p != NULL)
    {
        if(strcmp(p->ip, ipbuf) == 0 && p->port == delport)
        {
            break;
        }
        q = p;
        p = p->next;
    }

    if(p == NULL)
    {
        return -1;
    }

    q->next = p->next;
    p->next = NULL;

    // 这里一定要关闭socket，不然客户端断开后，服务器文件描述符会一直泄漏
    if(p->sock >= 0)
    {
        close(p->sock);
        p->sock = -1;
    }

    free(p);
    return 0;
}