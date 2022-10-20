#include <cstdlib>
#include <cstdio>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include "threadpool.h"
#include "httpserver.h"

const int MAX_EVENT_NUMBER = 10000;//epoll的最大监听事件数
const int MAX_FD = 65536;		   //最大保持http的fd数量

int main(int argc , char** argv)
{
	//获取port
	if (argc < 2)
	{
		printf("启动参数错误！\n");
		exit(1);
	}
	int port = atoi(argv[1]);
	//SIGPIPE设置
	// signal(SIGPIPE, SIG_IGN);
	struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = SIG_IGN;
    sigfillset( &sa.sa_mask );
    assert( sigaction( SIGPIPE, &sa, NULL ) != -1 );
	
	//socket
	int fd_listen = socket(AF_INET, SOCK_STREAM, 0);
	if (fd_listen < 0)
	{
		printf("socket创建失败！\n");
		exit(1);
	}
	sockaddr_in addr_server;
	memset(&addr_server, 0, sizeof(addr_server));
	addr_server.sin_addr.s_addr = INADDR_ANY;
	addr_server.sin_family = AF_INET;
	addr_server.sin_port = htons(port);
	int reuse = 1;
	setsockopt(fd_listen, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
	if ( bind(fd_listen, (sockaddr*)&addr_server, sizeof(addr_server))== -1)
	{
		printf("绑定socket失败！%s\n",strerror(errno));
		exit(1);
	}
	if (listen(fd_listen, 10) == -1)//第二个参数？
	{
		printf("监听失败！\n");
		exit(1);
	}

	//epoll
	int fd_epoll = epoll_create(1);
	if (fd_epoll < 0)
	{
		printf("epoll创建失败\n");
		exit(1);
	}
	epoll_event event;
	event.data.fd = fd_listen;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd_listen, &event) < 0)
	{
		printf("epoll设置失败\n");
		exit(1);
	}
	epoll_event events[MAX_EVENT_NUMBER];
	memset(events, 0, sizeof(events));
	HttpServer::epollfd_ = fd_epoll;


	//创建线程池
	ThreadPool pool;
	HttpServer* httpservers = new HttpServer[MAX_FD];
	while (1)
	{
		int num = epoll_wait(fd_epoll, events, MAX_EVENT_NUMBER, -1);
		if (num == -1)
		{
			printf("epoll出错\n");
			exit(1);
		}
		for (int i = 0; i < num; ++i)
		{
			int fd = events[i].data.fd;
			if (fd == fd_listen)//建立新的连接
			{
				sockaddr_in addr_clinet;
				memset(&addr_clinet, 0, sizeof(addr_clinet));
				socklen_t len_addr_clinet = sizeof(addr_clinet);
				int fd_client = accept(fd_listen, (sockaddr*)&addr_clinet, &len_addr_clinet);
				if (fd_client < 0)
				{
					printf("建立连接失败\n");
					exit(1);
				}

				memset(&event, 0, sizeof(event));
				event.data.fd = fd_client;
				event.events = EPOLLET | EPOLLIN;
				if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd_client, &event) < 0)
				{
					printf("epoll添加失败\n");
					exit(1);
				}

				int old_option = fcntl(fd_client, F_GETFL);
				int new_option = old_option | O_NONBLOCK;
				if (fcntl(fd_client, F_SETFL, new_option) == -1)
				{
					perror("设置非阻塞");
					exit(1);
				}

				httpservers[fd_client].Init(fd_client, addr_clinet);
			}
			else if (events[i].events & EPOLLIN)
			{
				if(httpservers[fd].Read())
				{
					pool.AddTask(&httpservers[fd]);
					// httpservers[fd].Process();
				}
				
			}
			else if(events[i].events & EPOLLOUT)
			{
				httpservers[fd].Write();
			}
		}
	}
	close(fd_listen);
	pool.Destory();
	delete[] httpservers;
	return 0;
}
