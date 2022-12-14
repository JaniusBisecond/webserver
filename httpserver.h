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
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/uio.h>

class HttpServer
{
public:
	static int epollfd_;
	static int usernum_;
	const char *ROOT_PATH = "/home/janius/projects/webserver/resources";
	const int BUF_SIZE = 2048; //读写缓冲区大小
	const int SAVE_SIZE = 64;
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
		Parseing = 0,			   //正在解析
		OK = 200,				   //客户端请求成功
		BadRequest = 400,		   //客户端请求有语法错误，不能被服务器所理解
		Unauthorized = 401,		   //请求未经授权，这个状态代码必须和WWW-Authenticate报头域一起使用
		Forbidden = 403,		   //服务器收到请求，但是拒绝提供服务
		NotFound = 404,			   //请求资源不存在，eg：输入了错误的URL
		InternalServerError = 500, //服务器发生不可预期的错误
		ServerUnavailable = 503,   //服务器当前不能处理客户端的请求，一段时间后可能恢复正常
	};
	enum CheckState
	{
		CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
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
	void modfd(int epollfd, int fd, int ev);

private:
	const char *GetLine();
	Code ParseRequestLine(const char *line);
	Code ParseHeader(const char *header);
	bool GenResponse(char *response);
	Code Parse();
	
	template<typename T>										//可变参数传入
	const T& VaArg(const T& t);
	const char* VaArg(const char*& str);
	template<typename...Args>
	bool AddResponse(const char* format, const Args&... rest);
	bool AddStatusLine(Code code,const char* info);
	bool AddHeaders(const size_t& filesize);
	bool AddContentLength(const size_t& filesize);
	bool AddContentType();
	bool AddBlankLine();
	bool AddLinger();
	bool AddContent(const char* str);
	


private:
	int fd_;
	sockaddr_in addr_;
	char *buf_; 	 //接收请求
	char *prebuf_;
	char *response_; // 响应内容
	int response_idx_;//响应追加指针
	Method method_;  //请求方法
	char *url_;
	char *version_;
	char *host_;
	Code code_;
	char *path_;
	char *fileaddress_;
	bool keepalive_;
	struct stat filestat_;
	CheckState linestate_; //行解析状态
	iovec iov_[2];
	int iov_count_;
	int sendnum_;
};
