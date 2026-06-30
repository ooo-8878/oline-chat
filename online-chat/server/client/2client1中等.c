#include "myhead.h"  
int tcpsock;

// 发满指定字节 
int send_n(int sockfd, const void *buf, int len)
{
    int total = 0;
    int ret;
    const char *p = (const char *)buf;

    while(total < len)
    {
        ret = send(sockfd, p + total, len - total, 0);
        if(ret < 0)
        {
            if(errno == EINTR) continue;
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

// 收满指定字节 
int recv_n(int sockfd, void *buf, int len)
{
    int total = 0;
    int ret;
    char *p = (char *)buf;

    while(total < len)
    {
        ret = recv(sockfd, p + total, len - total, 0);
        if(ret < 0)
        {
            if(errno == EINTR) continue;
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

// 发送一条字符串协议：先发长度，再发内容 
int send_str_to_server(const char *str)
{
    int msglen;

    if(str == NULL) return -1;

    msglen = strlen(str);

    if(send_n(tcpsock, &msglen, sizeof(msglen)) < 0)
        return -1;

    if(send_n(tcpsock, str, msglen) < 0)
        return -1;

    return 0;
}

//接收线程 
void *recv_servermsg(void *arg)
{
    char rbuf[4096];
    int msglen;
    (void)arg;

    while(1)
    {
        char tmpbuf[4096];
        char *p1, *p2, *p3, *p4, *p5;

        msglen = 0;
        if(recv_n(tcpsock, &msglen, sizeof(msglen)) < 0)
        {
            printf("服务器断开连接了!\n");
            exit(0);
        }

        if(msglen <= 0 || msglen >= (int)sizeof(rbuf))
        {
            printf("收到异常消息长度: %d\n", msglen);
            exit(0);
        }

        bzero(rbuf, sizeof(rbuf));
        if(recv_n(tcpsock, rbuf, msglen) < 0)
        {
            printf("接收消息失败!\n");
            exit(0);
        }

        rbuf[msglen] = '\0';

        if(strcmp(rbuf, "quit") == 0)
        {
            printf("收到退出消息，客户端退出。\n");
            exit(0);
        }

        /* 在线列表：格式一般是 ip:port@ip:port@... */
        if(strchr(rbuf, ':') != NULL &&
           strstr(rbuf, "msg@") == NULL &&
           strstr(rbuf, "file@") == NULL &&
           strstr(rbuf, "emoji@") == NULL)
        {
            printf("\n========== 在线列表 ==========\n");
            printf("%s\n", rbuf);
            printf("================================\n");
            continue;
        }

        bzero(tmpbuf, sizeof(tmpbuf));
        strncpy(tmpbuf, rbuf, sizeof(tmpbuf) - 1);

        p1 = strtok(tmpbuf, "@");
        if(p1 == NULL)
        {
            printf("服务器给我发送的信息是: %s\n", rbuf);
            continue;
        }

        //  普通消息 
        if(strcmp(p1, "msg") == 0)
        {
            p2 = strtok(NULL, "@");   // 发送方ip
            p3 = strtok(NULL, "@");   // 发送方端口
            p4 = strtok(NULL, "");    // 真正消息内容

            if(p2 && p3 && p4)
                printf("\n[%s:%s] %s\n", p2, p3, p4);
            else
                printf("服务器给我发送的信息是: %s\n", rbuf);

            continue;
        }

        // 文件消息
        if(strcmp(p1, "file") == 0)
        {
            long filesize;
            long remaining;
            int fd;
            int once;
            char savepath[512];
            char filebuf[1024];

            p2 = strtok(NULL, "@");   // 发送方ip
            p3 = strtok(NULL, "@");   // 发送方端口
            p4 = strtok(NULL, "@");   // 文件名
            p5 = strtok(NULL, "@");   // 文件大小

            if(p2 == NULL || p3 == NULL || p4 == NULL || p5 == NULL)
            {
                printf("收到异常文件协议: %s\n", rbuf);
                continue;
            }

            filesize = atol(p5);
            remaining = filesize;

            mkdir("./recvfiles", 0777);
            snprintf(savepath, sizeof(savepath), "./recvfiles/%s", p4);

            fd = open(savepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(fd == -1)
            {
                perror("打开接收文件失败");

                while(remaining > 0)
                {
                    once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;
                    if(recv_n(tcpsock, filebuf, once) < 0)
                        break;
                    remaining -= once;
                }

                printf("接收文件失败: %s\n", p4);
                continue;
            }

            while(remaining > 0)
            {
                once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;

                if(recv_n(tcpsock, filebuf, once) < 0)
                {
                    perror("接收文件内容失败");
                    close(fd);
                    fd = -1;
                    break;
                }

                if(write(fd, filebuf, once) != once)
                {
                    perror("写入接收文件失败");
                    close(fd);
                    fd = -1;
                    break;
                }

                remaining -= once;
            }

            if(fd != -1)
                close(fd);

            if(remaining <= 0)
                printf("收到文件成功: %s -> %s\n", p4, savepath);
            else
                printf("接收文件中断: %s\n", p4);

            continue;
        }

        // 表情消息 
        if(strcmp(p1, "emoji") == 0)
        {
            long emojisize;
            long remaining;
            int fd;
            int once;
            char savepath[512];
            char filebuf[1024];

            p2 = strtok(NULL, "@");   // 发送方ip
            p3 = strtok(NULL, "@");   // 发送方端口
            p4 = strtok(NULL, "@");   // 表情大小

            if(p2 == NULL || p3 == NULL || p4 == NULL)
            {
                printf("收到异常表情协议: %s\n", rbuf);
                continue;
            }

            emojisize = atol(p4);
            remaining = emojisize;

            mkdir("./recvemoji", 0777);
            snprintf(savepath, sizeof(savepath), "./recvemoji/emoji_%s_%s.jpg", p2, p3);

            fd = open(savepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(fd == -1)
            {
                perror("打开接收表情文件失败");

                while(remaining > 0)
                {
                    once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;
                    if(recv_n(tcpsock, filebuf, once) < 0)
                        break;
                    remaining -= once;
                }

                printf("接收表情失败\n");
                continue;
            }

            while(remaining > 0)
            {
                once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;

                if(recv_n(tcpsock, filebuf, once) < 0)
                {
                    perror("接收表情内容失败");
                    close(fd);
                    fd = -1;
                    break;
                }

                if(write(fd, filebuf, once) != once)
                {
                    perror("写入接收表情文件失败");
                    close(fd);
                    fd = -1;
                    break;
                }

                remaining -= once;
            }

            if(fd != -1)
                close(fd);

            if(remaining <= 0)
                printf("收到表情成功: %s\n", savepath);
            else
                printf("接收表情中断\n");

            continue;
        }

        printf("服务器给我发送的信息是: %s\n", rbuf);
    }

    return NULL;
}

int main()
{
    int ret;
    int n;
    char sbuf[256];
    char filepath[256];
    char otherip[20];
    unsigned short otherport;
    char allbuf[2048];
    pthread_t id;

    struct sockaddr_in bindaddr;
    bzero(&bindaddr, sizeof(bindaddr));
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = inet_addr("192.168.11.44");
    bindaddr.sin_port = htons(10000);

    struct sockaddr_in serveraddr;
    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr("192.168.11.44");
    serveraddr.sin_port = htons(30000);

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
        perror("绑定ip和端口号失败");
        close(tcpsock);
        return -1;
    }

    ret = connect(tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if(ret == -1)
    {
        perror("连接服务器失败");
        close(tcpsock);
        return -1;
    }

    pthread_create(&id, NULL, recv_servermsg, NULL);

    while(1)
    {
        printf("\n请输入您的选择!\n");
        printf("1. 获取服务器上所有在线的客户端信息\n");
        printf("2. 跟某个客户端单聊\n");
        printf("3. 跟某个客户端发送文件\n");
        printf("4. 跟某个客户端发送表情包\n");
        printf("5. 退出\n");
        scanf("%d", &n);

        switch(n)
        {
            case 1:
            {
                send_str_to_server("getlist");
                break;
            }

            case 2:
            {
                bzero(otherip, sizeof(otherip));
                bzero(sbuf, sizeof(sbuf));
                bzero(allbuf, sizeof(allbuf));

                printf("请输入对方的ip:\n");
                scanf("%s", otherip);

                printf("请输入对方的端口号:\n");
                scanf("%hu", &otherport);

                printf("请输入要发送给这个客户端的真实信息:\n");
                scanf("%s", sbuf);

                sprintf(allbuf, "msg@%s@%hu@%s", otherip, otherport, sbuf);

                if(send_str_to_server(allbuf) < 0)
                    printf("发送普通消息失败!\n");

                break;
            }

            case 3:
            {
                int fd;
                struct stat mystat;
                long filesize;
                char *filename;
                char filebuf[1024];

                bzero(otherip, sizeof(otherip));
                bzero(filepath, sizeof(filepath));
                bzero(allbuf, sizeof(allbuf));

                printf("请输入对方的ip:\n");
                scanf("%s", otherip);

                printf("请输入对方的端口号:\n");
                scanf("%hu", &otherport);

                printf("请输入要发送给这个客户端的文件路径名:\n");
                scanf("%s", filepath);

                fd = open(filepath, O_RDONLY);
                if(fd == -1)
                {
                    perror("打开文件失败");
                    break;
                }

                if(stat(filepath, &mystat) == -1)
                {
                    perror("获取文件大小失败");
                    close(fd);
                    break;
                }

                filesize = mystat.st_size;

                filename = strrchr(filepath, '/');
                if(filename != NULL)
                    filename++;
                else
                    filename = filepath;

                sprintf(allbuf, "file@%s@%hu@%s@%ld", otherip, otherport, filename, filesize);

                if(send_str_to_server(allbuf) < 0)
                {
                    printf("发送文件协议失败!\n");
                    close(fd);
                    break;
                }

                while(1)
                {
                    ret = read(fd, filebuf, sizeof(filebuf));
                    if(ret < 0)
                    {
                        perror("读取文件失败");
                        close(fd);
                        break;
                    }

                    if(ret == 0)
                        break;

                    if(send_n(tcpsock, filebuf, ret) < 0)
                    {
                        printf("发送文件内容失败!\n");
                        close(fd);
                        break;
                    }
                }

                close(fd);
                printf("文件发送完成: %s\n", filename);
                break;
            }

            case 4:
            {
                int fd;
                struct stat mystat;
                long filesize;
                char filebuf[1024];

                bzero(otherip, sizeof(otherip));
                bzero(filepath, sizeof(filepath));
                bzero(allbuf, sizeof(allbuf));

                printf("请输入对方的ip:\n");
                scanf("%s", otherip);

                printf("请输入对方的端口号:\n");
                scanf("%hu", &otherport);

                printf("请输入要发送给这个客户端的表情文件路径名:\n");
                scanf("%s", filepath);

                fd = open(filepath, O_RDONLY);
                if(fd == -1)
                {
                    perror("打开表情包失败");
                    break;
                }

                if(stat(filepath, &mystat) == -1)
                {
                    perror("获取表情包大小失败");
                    close(fd);
                    break;
                }

                filesize = mystat.st_size;

                sprintf(allbuf, "emoji@%s@%hu@%ld", otherip, otherport, filesize);

                if(send_str_to_server(allbuf) < 0)
                {
                    printf("发送表情协议失败!\n");
                    close(fd);
                    break;
                }

                while(1)
                {
                    ret = read(fd, filebuf, sizeof(filebuf));
                    if(ret < 0)
                    {
                        perror("读取表情包失败");
                        close(fd);
                        break;
                    }

                    if(ret == 0)
                        break;

                    if(send_n(tcpsock, filebuf, ret) < 0)
                    {
                        printf("发送表情内容失败!\n");
                        close(fd);
                        break;
                    }
                }

                close(fd);
                printf("表情发送完成!\n");
                break;
            }

            case 5:
            {
                close(tcpsock);
                return 0;
            }

            default:
                printf("输入有误，请重新选择!\n");
                break;
        }
    }

    close(tcpsock);
    return 0;
}