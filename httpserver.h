#pragma once
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <signal.h>

class HttpServer
{
public:
	static int epollfd_;
	static int usernum_;
	const char *ROOT_PATH = "/home/janius/projects/webserver/resources";
	const int BUF_SIZE = 4096; //读写缓冲区大小
	const int SAVE_SIZE = 128;
	int FILE_SIZE = SAVE_SIZE;

enum Method
	{
		Get = 0,
		Post,
		Head,
		Put,
		Delete,
		Trace,
		Options,
		Connect
	};
	enum Code
	{
		OK = 200,				   //客户端请求成功
		BadRequest = 400,		   //客户端请求有语法错误，不能被服务器所理解
		Unauthorized = 401,		   //请求未经授权，这个状态代码必须和WWW-Authenticate报头域一起使用
		Forbidden = 403,		   //服务器收到请求，但是拒绝提供服务
		NotFound = 404,			   //请求资源不存在，eg：输入了错误的URL
		InternalServerError = 500, //服务器发生不可预期的错误
		ServerUnavailable = 503,   //服务器当前不能处理客户端的请求，一段时间后可能恢复正常
	};

public:
	HttpServer();
	HttpServer(const HttpServer &httpserver);
	HttpServer& operator=(const HttpServer &);
	~HttpServer();
	void Init(int fd, sockaddr_in addr);
	bool Read();
	bool Write();
	bool Process();
	void CloseConnect();

private:
	const char *GetLine();
	Code ParseLine(const char *line);
	Code ParseHeader(const char *header);
	bool GenResponse(char *response);
	bool Parse();

private:
	int fd_;
	sockaddr_in addr_;
	char *buf_; //接收请求
	char *response_; // 响应内容
	char *prebuf_;
	Method method_; //请求方法
	char *url_;
	char *version_;
	char *host_;
	Code code_;
	char *path_;
};
