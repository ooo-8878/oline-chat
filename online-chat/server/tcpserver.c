#include "list.h"

#define USER_FILE "./user.txt"

struct clientlist *myhead;
int flag = 0;

// 用互斥锁保护在线客户端链表，避免多线程同时遍历/删除/插入时崩溃
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

// 校验注册账号密码的合法性
int check_userinfo_valid(const char *user, const char *pass, const char *phone)
{
    int i;

    if(user == NULL || pass == NULL || phone == NULL)
    {
        return 0;
    }
    if(strlen(user) == 0 || strlen(pass) == 0 || strlen(phone) == 0)
    {
        return 0;
    }
    if(strlen(user) > 20 || strlen(pass) > 20 || strlen(phone) != 11)
    {
        return 0;
    }
    if(strchr(user, ' ') != NULL || strchr(pass, ' ') != NULL || strchr(phone, ' ') != NULL)
    {
        return 0;
    }

    for(i = 0; phone[i] != '\0'; i++)
    {
        if(phone[i] < '0' || phone[i] > '9')
            return 0;
    }

    return 1;
}

// 登录校验
int check_login(const char *user, const char *pass)
{
    FILE *fp = fopen(USER_FILE, "r");
    if(fp == NULL)
    {
        perror("open user.txt failed");
        return 0;
    }

    char file_user[64];
    char file_pass[64];
    char file_phone[64];

    while(fscanf(fp, "%63s %63s %63s", file_user, file_pass, file_phone) != EOF)
    {
        if(strcmp(user, file_user) == 0 && strcmp(pass, file_pass) == 0)
        {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

// 注册账号写入
int register_user(const char *user, const char *pass, const char *phone)
{
    printf("开始注册: %s %s %s\n", user, pass, phone);
    printf("准备写入文件: %s\n", USER_FILE);

    FILE *fp = fopen(USER_FILE, "a+");
    if(fp == NULL)
    {
        perror("open user.txt failed");
        return 0;
    }

    char file_user[64];
    char file_pass[64];
    char file_phone[64];

    rewind(fp);

    while(fscanf(fp, "%63s %63s %63s", file_user, file_pass, file_phone) != EOF)
    {
        if(strcmp(user, file_user) == 0)
        {
            fclose(fp);
            return -1; // 账号已存在
        }
    }

    fprintf(fp, "%s %s %s\n", user, pass, phone);
    fflush(fp);
    fclose(fp);

    printf("注册写入成功\n");
    return 1;
}

// 找回时候手机号校验
int find_user_phone_pass(const char *user, const char *phone, char *passbuf, int size)
{
    FILE *fp = fopen(USER_FILE, "r");
    if(fp == NULL)
    {
        perror("open user.txt failed");
        return -1;
    }

    char file_user[64];
    char file_pass[64];
    char file_phone[64];

    while(fscanf(fp, "%63s %63s %63s", file_user, file_pass, file_phone) != EOF)
    {
        if(strcmp(user, file_user) == 0)
        {
            if(strcmp(phone, file_phone) == 0)
            {
                strncpy(passbuf, file_pass, size - 1);
                passbuf[size - 1] = '\0';
                fclose(fp);
                return 1;   // 账号和手机号都匹配
            }
            else
            {
                fclose(fp);
                return 0;   // 账号存在，但手机号不匹配
            }
        }
    }

    fclose(fp);
    return -1; // 用户不存在
}

static int recv_n(int sockfd, void *buf, int len)
{
    int total = 0;
    int ret;
    char *p = (char *)buf;

    while(total < len)
    {
        ret = recv(sockfd, p + total, len - total, 0);
        if(ret < 0)
        {
            if(errno == EINTR)
                continue;
            perror("recv failed");
            return -1;
        }
        if(ret == 0)
        {
            return -1;
        }
        total += ret;
    }

    return 0;
}

static int send_n(int sockfd, const void *buf, int len)
{
    int total = 0;
    int ret;
    const char *p = (const char *)buf;

    while(total < len)
    {
        ret = send(sockfd, p + total, len - total, 0);
        if(ret < 0)
        {
            if(errno == EINTR)
                continue;
            perror("send failed");
            return -1;
        }
        if(ret == 0)
        {
            return -1;
        }
        total += ret;
    }

    return 0;
}

int send_proto_msg(int sock, const char *buf)
{
    int len;

    if(buf == NULL)
        return -1;

    len = strlen(buf);

    if(send_n(sock, &len, sizeof(len)) < 0)
        return -1;

    if(len > 0)
    {
        if(send_n(sock, buf, len) < 0)
            return -1;
    }

    return 0;
}

// 查找目标客户端的socket
// 注意：这里返回sock，而不是返回链表节点指针
// 原因是节点可能在函数返回后被别的线程删除，返回sock更安全
static int find_target_sock(const char *ip, unsigned short port)
{
    struct clientlist *temp;
    int sock = -1;

    pthread_mutex_lock(&client_mutex);

    temp = myhead->next;
    while(temp != NULL)
    {
        if(strcmp(temp->ip, ip) == 0 && temp->port == port && temp->is_online == 1)
        {
            sock = temp->sock;
            break;
        }
        temp = temp->next;
    }

    pthread_mutex_unlock(&client_mutex);
    return sock;
}

// 删除客户端节点（线程安全封装）
static void remove_client_safe(const char *ip, unsigned short port)
{
    pthread_mutex_lock(&client_mutex);
    list_delete((char *)ip, port, myhead);
    pthread_mutex_unlock(&client_mutex);
}

// 线程的任务函数：专门负责接收某个客户端发送过来的信息
void *recv_client_msg(void *arg)
{
    int ret;
    struct clientlist *p = (struct clientlist *)arg;
    char clientbuf[2048];
    char rbuf[2048];
    int msglen = 0;

    while(1)
    {
        char self_ip[20] = {0};
        unsigned short self_port = 0;
        int self_sock = -1;

        // 这里先把自己的关键信息拷贝出来
        // 因为后面节点一旦被删除，p就不能再访问了
        strcpy(self_ip, p->ip);
        self_port = p->port;
        self_sock = p->sock;

        bzero(rbuf, sizeof(rbuf));
        msglen = 0;

        ret = recv_n(self_sock, &msglen, sizeof(msglen));
        if(ret < 0)
        {
            printf("客户端ip %s 端口号%hu断开了\n", self_ip, self_port);

            pthread_mutex_lock(&client_mutex);
            if(p->is_online == 1)
                flag = 1;
            pthread_mutex_unlock(&client_mutex);

            remove_client_safe(self_ip, self_port);
            pthread_exit(NULL);
        }

        if(msglen < 0 || msglen >= (int)sizeof(rbuf))
        {
            printf("非法消息长度: %d\n", msglen);

            pthread_mutex_lock(&client_mutex);
            if(p->is_online == 1)
                flag = 1;
            pthread_mutex_unlock(&client_mutex);

            remove_client_safe(self_ip, self_port);
            pthread_exit(NULL);
        }

        if(msglen == 0)
        {
            continue;
        }

        bzero(rbuf, sizeof(rbuf));

        ret = recv_n(self_sock, rbuf, msglen);
        if(ret < 0)
        {
            printf("客户端ip %s 端口号%hu断开了\n", self_ip, self_port);

            pthread_mutex_lock(&client_mutex);
            if(p->is_online == 1)
                flag = 1;
            pthread_mutex_unlock(&client_mutex);

            remove_client_safe(self_ip, self_port);
            pthread_exit(NULL);
        }

        rbuf[msglen] = '\0';

        /*
            判断接收的信息,服务器收到的信息有四种类型(按照我们制定的通信协议)
               1.getlist
               2.聊天文字信息  msg@对方ip@对方端口@真实信息
               3.文件信息      file@对方ip@对方端口@文件名@文件大小
               4.表情包信息    emoji@对方ip@对方端口@表情包大小
               5.登录和注册
        */

        if(strcmp(rbuf, "online") == 0) // 在线判断
        {
            pthread_mutex_lock(&client_mutex);
            p->is_online = 1;
            flag = 1;
            pthread_mutex_unlock(&client_mutex);

            send_proto_msg(self_sock, "online_ok");
            continue;
        }

        // 获取在线
        if(strcmp(rbuf, "getlist") == 0)
        {
            struct clientlist *temp;

            bzero(clientbuf, sizeof(clientbuf));
            strcpy(clientbuf, "list@");

            pthread_mutex_lock(&client_mutex);

            temp = myhead->next;
            while(temp != NULL)
            {
                if(temp->is_online == 1)
                {
                    if(!(strcmp(temp->ip, self_ip) == 0 && temp->port == self_port))
                    {
                        snprintf(clientbuf + strlen(clientbuf),
                                 sizeof(clientbuf) - strlen(clientbuf),
                                 "%s:%hu@",
                                 temp->ip, temp->port);
                    }
                }
                temp = temp->next;
            }

            pthread_mutex_unlock(&client_mutex);

            // printf("发送给客户端的在线列表协议: [%s]\n", clientbuf);
            send_proto_msg(self_sock, clientbuf);
            continue;
        }
        else
        {
            char tmpbuf[2048];
            bzero(tmpbuf, sizeof(tmpbuf));
            strcpy(tmpbuf, rbuf);

            char *p1 = strtok(tmpbuf, "@");
            if(p1 == NULL)
                continue;

            // 登录
            if(strcmp(p1, "login") == 0)
            {
                char *p2 = strtok(NULL, "@"); // 账号
                char *p3 = strtok(NULL, "@"); // 密码

                if(p2 == NULL || p3 == NULL)
                {
                    send_proto_msg(self_sock, "invalid");
                    continue;
                }

                // 这里也加上基本合法性判断，避免空格式乱传
                if(strlen(p2) == 0 || strlen(p3) == 0)
                {
                    send_proto_msg(self_sock, "invalid");
                    continue;
                }

                if(check_login(p2, p3))
                {
                    send_proto_msg(self_sock, "ok");
                }
                else
                {
                    send_proto_msg(self_sock, "fail");
                }
                continue;
            }

            // 注册
            if(strcmp(p1, "register") == 0)
            {
                char *p2 = strtok(NULL, "@"); // 账号
                char *p3 = strtok(NULL, "@"); // 密码
                char *p4 = strtok(NULL, "@"); // 手机号

                if(p2 == NULL || p3 == NULL || p4 == NULL)
                {
                    send_proto_msg(self_sock, "invalid");
                    continue;
                }

                // 合法性校验
                if(!check_userinfo_valid(p2, p3, p4))
                {
                    send_proto_msg(self_sock, "invalid");
                    continue;
                }

                {
                    int reg_ret = register_user(p2, p3, p4);
                    if(reg_ret == 1)
                    {
                        send_proto_msg(self_sock, "ok");
                    }
                    else if(reg_ret == -1)
                    {
                        send_proto_msg(self_sock, "user_exist");
                    }
                    else
                    {
                        send_proto_msg(self_sock, "fail");
                    }
                }
                continue;
            }

            // 账号和手机号校验
            if(strcmp(p1, "phonecheck") == 0)
            {
                char *p2 = strtok(NULL, "@"); // 账号
                char *p3 = strtok(NULL, "@"); // 手机号
                char passbuf[64] = {0};

                if(p2 == NULL || p3 == NULL)
                {
                    send_proto_msg(self_sock, "invalid");
                    continue;
                }

                {
                    int ck = find_user_phone_pass(p2, p3, passbuf, sizeof(passbuf));
                    if(ck == 1)
                    {
                        send_proto_msg(self_sock, "ok");
                    }
                    else if(ck == 0)
                    {
                        send_proto_msg(self_sock, "phone_error");
                    }
                    else
                    {
                        send_proto_msg(self_sock, "no_user");
                    }
                }
                continue;
            }

            // 验证码校验 + 返回密码
            // 注意：这里不再依赖服务器全局缓存，避免多个客户端同时找回时串号
            if(strcmp(p1, "findpass") == 0)
            {
                char *p2 = strtok(NULL, "@"); // 账号
                char *p3 = strtok(NULL, "@"); // 手机号
                char *p4 = strtok(NULL, "@"); // 验证码
                char sendbuf[128] = {0};
                char passbuf[64] = {0};

                if(p2 == NULL || p3 == NULL || p4 == NULL)
                {
                    send_proto_msg(self_sock, "invalid");
                    continue;
                }

                // 这里服务器不校验验证码内容，因为验证码已经在客户端本地校验过
                // 服务器只重新核验账号和手机号，再把密码返回
                {
                    int ck = find_user_phone_pass(p2, p3, passbuf, sizeof(passbuf));
                    if(ck == 1)
                    {
                        snprintf(sendbuf, sizeof(sendbuf), "pass@%s", passbuf);
                        send_proto_msg(self_sock, sendbuf);
                    }
                    else if(ck == 0)
                    {
                        send_proto_msg(self_sock, "phone_error");
                    }
                    else
                    {
                        send_proto_msg(self_sock, "no_user");
                    }
                }
                continue;
            }

            // 文字聊天消息
            if(strcmp(p1, "msg") == 0)
            {
                char *p2 = strtok(NULL, "@"); // 对方ip
                char *p3 = strtok(NULL, "@"); // 对方端口
                char *p4 = strtok(NULL, "");  // 真实信息
                int target_sock;

                if(p2 == NULL || p3 == NULL || p4 == NULL)
                    continue;

                target_sock = find_target_sock(p2, atoi(p3));
                if(target_sock < 0)
                {
                    printf("客户端%s:%s不在线\n", p2, p3);
                    continue;
                }

                bzero(clientbuf, sizeof(clientbuf));
                snprintf(clientbuf, sizeof(clientbuf), "msg@%s@%hu@%s", self_ip, self_port, p4);

                send_proto_msg(target_sock, clientbuf);
                continue;
            }

            // 文件消息：服务器转发给目标客户端
            if(strcmp(p1, "file") == 0)
            {
                char *p2 = strtok(NULL, "@"); // 对方ip
                char *p3 = strtok(NULL, "@"); // 对方端口
                char *p4 = strtok(NULL, "@"); // 文件名
                char *p5 = strtok(NULL, "@"); // 文件大小
                int target_sock;
                long filesize;
                long remainingbytes;
                char buf[1024];

                if(p2 == NULL || p3 == NULL || p4 == NULL || p5 == NULL)
                    continue;

                filesize = atol(p5);
                printf("收到文件协议：发送方[%s:%hu] -> 接收方[%s:%s] 文件名[%s] 大小[%ld]\n",
                       self_ip, self_port, p2, p3, p4, filesize);

                target_sock = find_target_sock(p2, atoi(p3));
                if(target_sock < 0)
                {
                    printf("目标客户端 %s:%s 不在线，文件转发失败\n", p2, p3);

                    // 即使目标不在线，也要把后续文件数据读掉
                    remainingbytes = filesize;
                    while(remainingbytes > 0)
                    {
                        int once = remainingbytes > (long)sizeof(buf) ? sizeof(buf) : (int)remainingbytes;
                        if(recv_n(self_sock, buf, once) < 0)
                        {
                            perror("recv file data failed");
                            break;
                        }
                        remainingbytes -= once;
                    }
                    continue;
                }

                // 先把协议头转发给目标客户端
                bzero(clientbuf, sizeof(clientbuf));
                snprintf(clientbuf, sizeof(clientbuf), "file@%s@%hu@%s@%ld", self_ip, self_port, p4, filesize);

                if(send_proto_msg(target_sock, clientbuf) == -1)
                {
                    printf("转发文件协议头失败\n");

                    // 协议头发送失败，也要把原发送方发过来的文件内容读掉，避免后面协议错位
                    remainingbytes = filesize;
                    while(remainingbytes > 0)
                    {
                        int once = remainingbytes > (long)sizeof(buf) ? sizeof(buf) : (int)remainingbytes;
                        if(recv_n(self_sock, buf, once) < 0)
                            break;
                        remainingbytes -= once;
                    }
                    continue;
                }

                // 再转发文件二进制数据
                remainingbytes = filesize;

                while(remainingbytes > 0)
                {
                    int once = remainingbytes > (long)sizeof(buf) ? sizeof(buf) : (int)remainingbytes;
                    if(recv_n(self_sock, buf, once) < 0)
                    {
                        perror("recv file data failed");
                        break;
                    }

                    if(send_n(target_sock, buf, once) < 0)
                    {
                        perror("send file data failed");
                        break;
                    }

                    remainingbytes -= once;
                    printf("文件已转发 %d 字节，剩余 %ld 字节\n", once, remainingbytes);
                }

                if(remainingbytes <= 0)
                    printf("文件转发完成！\n");

                continue;
            }

            // 表情包消息：服务器转发给目标客户端
            if(strcmp(p1, "emoji") == 0)
            {
                char *p2 = strtok(NULL, "@"); // 对方ip
                char *p3 = strtok(NULL, "@"); // 对方端口
                char *p4 = strtok(NULL, "@"); // 表情包大小
                int target_sock;
                long size;
                long remainingbytes;
                char buf[1024];

                if(p2 == NULL || p3 == NULL || p4 == NULL)
                    continue;

                size = atol(p4);
                printf("收到表情包协议：发送方[%s:%hu] -> 接收方[%s:%s] 大小[%ld]\n",
                       self_ip, self_port, p2, p3, size);

                target_sock = find_target_sock(p2, atoi(p3));
                if(target_sock < 0)
                {
                    printf("目标客户端 %s:%s 不在线，表情包转发失败\n", p2, p3);

                    // 目标不在线，也要把发送方后续发来的表情包数据读掉
                    remainingbytes = size;
                    while(remainingbytes > 0)
                    {
                        int once = remainingbytes > (long)sizeof(buf) ? sizeof(buf) : (int)remainingbytes;
                        if(recv_n(self_sock, buf, once) < 0)
                        {
                            perror("recv emoji data failed");
                            break;
                        }
                        remainingbytes -= once;
                    }
                    continue;
                }

                // 先把协议头转发给目标客户端
                bzero(clientbuf, sizeof(clientbuf));
                snprintf(clientbuf, sizeof(clientbuf), "emoji@%s@%hu@%ld", self_ip, self_port, size);

                if(send_proto_msg(target_sock, clientbuf) == -1)
                {
                    printf("转发表情包协议头失败\n");

                    // 同样要把后续表情数据读掉，避免原发送方后面的协议错位
                    remainingbytes = size;
                    while(remainingbytes > 0)
                    {
                        int once = remainingbytes > (long)sizeof(buf) ? sizeof(buf) : (int)remainingbytes;
                        if(recv_n(self_sock, buf, once) < 0)
                            break;
                        remainingbytes -= once;
                    }
                    continue;
                }

                // 再转发表情包二进制数据
                remainingbytes = size;

                while(remainingbytes > 0)
                {
                    int once = remainingbytes > (long)sizeof(buf) ? sizeof(buf) : (int)remainingbytes;
                    if(recv_n(self_sock, buf, once) < 0)
                    {
                        perror("recv emoji data failed");
                        break;
                    }

                    if(send_n(target_sock, buf, once) < 0)
                    {
                        perror("send emoji data failed");
                        break;
                    }

                    remainingbytes -= once;
                    printf("表情包已转发 %d 字节，剩余 %ld 字节\n", once, remainingbytes);
                }

                if(remainingbytes <= 0)
                    printf("表情包转发完成！\n");

                continue;
            }
        }
    }

    {
        char self_ip[20] = {0};
        unsigned short self_port = 0;

        strcpy(self_ip, p->ip);
        self_port = p->port;

        printf("客户端ip %s 端口号%hu异常退出\n", self_ip, self_port);

        pthread_mutex_lock(&client_mutex);
        if(p->is_online == 1)
            flag = 1;
        pthread_mutex_unlock(&client_mutex);

        remove_client_safe(self_ip, self_port);
    }

    pthread_exit(NULL);
}

// 从服务器键盘输入内容发送给指定客户端
void *send_msgto_client(void *arg)
{
    char ipbuf[20];
    char sbuf[256];
    unsigned short someport;

    (void)arg;

    while(1)
    {
        int target_sock;

        bzero(ipbuf, sizeof(ipbuf));
        bzero(sbuf, sizeof(sbuf));

        printf("请输入你要跟哪个客户端聊天，输入这个客户端ip和端口号!\n");
        scanf("%19s", ipbuf);
        scanf("%hu", &someport);
        getchar(); // 吃掉前面的回车

        printf("请输入要给这个客户端发送的信息!\n");
        fgets(sbuf, sizeof(sbuf), stdin);

        if(strlen(sbuf) > 0 && sbuf[strlen(sbuf) - 1] == '\n')
            sbuf[strlen(sbuf) - 1] = '\0';

        target_sock = find_target_sock(ipbuf, someport);
        if(target_sock < 0)
        {
            printf("目标客户端不在线\n");
            continue;
        }

        send_proto_msg(target_sock, sbuf);
    }
}

// 线程任务函数：只要有客户端连接，都自动遍历链表打印
void *show_client(void *arg)
{
    (void)arg;

    while(1)
    {
        if(flag == 1)
        {
            struct clientlist *p;

            system("clear");

            pthread_mutex_lock(&client_mutex);

            p = myhead->next;
            while(p != NULL)
            {
                if(p->is_online == 1)
                {
                    printf("目前在线的客户端 %s %hu\n", p->ip, p->port);
                }
                p = p->next;
            }

            flag = 0;

            pthread_mutex_unlock(&client_mutex);
        }
        usleep(100000);
    }
}

int main()
{
    int ret;
    pthread_t id;
    pthread_t otherid1;
    pthread_t otherid2;
    int tcpsock;
    int newsock;

    struct sockaddr_in bindaddr;
    bzero(&bindaddr, sizeof(bindaddr));
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = inet_addr("192.168.11.44");
    bindaddr.sin_port = htons(30000);

    struct sockaddr_in clientaddr;
    socklen_t size = sizeof(clientaddr);

    pthread_attr_t myattr;
    pthread_attr_init(&myattr);
    pthread_attr_setdetachstate(&myattr, PTHREAD_CREATE_DETACHED);

    myhead = client_init();
    if(myhead == NULL)
        return -1;

    tcpsock = socket(AF_INET, SOCK_STREAM, 0);
    if(tcpsock == -1)
    {
        perror("创建tcp套接字失败");
        return -1;
    }

    {
        int on = 1;
        setsockopt(tcpsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    }

    ret = bind(tcpsock, (struct sockaddr *)&bindaddr, sizeof(bindaddr));
    if(ret == -1)
    {
        perror("绑定ip和端口号失败了");
        close(tcpsock);
        return -1;
    }

    ret = listen(tcpsock, 5);
    if(ret == -1)
    {
        perror("监听失败了");
        close(tcpsock);
        return -1;
    }

    pthread_create(&otherid1, NULL, show_client, NULL);
    pthread_create(&otherid2, NULL, send_msgto_client, NULL);

    while(1)
    {
        struct clientlist *newnode;

        newsock = accept(tcpsock, (struct sockaddr *)&clientaddr, &size);
        if(newsock == -1)
        {
            perror("accept failed");
            continue;
        }

        // 尾插链表时也要加锁
        pthread_mutex_lock(&client_mutex);
        newnode = list_tail(newsock, clientaddr, myhead);
        pthread_mutex_unlock(&client_mutex);

        if(newnode == NULL)
        {
            fprintf(stderr, "创建客户端节点失败\n");
            close(newsock);
            continue;
        }

        flag = 1;

        pthread_create(&id, &myattr, recv_client_msg, newnode);
    }

    close(tcpsock);
    close(newsock);
    pthread_attr_destroy(&myattr);

    return 0;
}