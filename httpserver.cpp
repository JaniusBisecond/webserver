#include "httpserver.h"

int HttpServer::epollfd_ = -1;
int HttpServer::usernum_ = 0;

HttpServer::HttpServer()
{
	fd_ = -1;
	memset(&addr_, 0, sizeof(addr_));
}

HttpServer::HttpServer(const HttpServer &httpserver)
{
	this->fd_ = httpserver.fd_;
	this->addr_ = httpserver.addr_;
	this->code_ = httpserver.OK;

	this->buf_ = new char[BUF_SIZE];
	strcpy(this->buf_, httpserver.buf_);
	this->prebuf_ = this->buf_;

	this->version_ = new char[SAVE_SIZE];
	strcpy(this->version_, httpserver.version_);

	this->url_ = new char[SAVE_SIZE];
	strcpy(this->url_, httpserver.url_);

	this->host_ = new char[SAVE_SIZE];
	strcpy(this->host_, httpserver.host_);
}

HttpServer &HttpServer::operator=(const HttpServer &rhs)
{
	this->fd_ = rhs.fd_;
	this->addr_ = rhs.addr_;
	this->code_ = rhs.OK;

	delete[] this->buf_;
	this->buf_ = new char[BUF_SIZE];
	strcpy(this->buf_, rhs.buf_);
	this->prebuf_ = this->buf_;

	delete[] this->version_;
	this->version_ = new char[SAVE_SIZE];
	strcpy(this->version_, rhs.version_);

	delete[] this->url_;
	this->url_ = new char[SAVE_SIZE];
	strcpy(this->url_, rhs.url_);

	delete[] this->host_;
	this->host_ = new char[SAVE_SIZE];
	strcpy(this->host_, rhs.host_);

	return *this;
}

HttpServer::~HttpServer()
{
	printf("溪沟里\n");
	if (buf_ != nullptr)
	{
		delete[] buf_;
		buf_ = nullptr;
	}
	if (version_ != nullptr)
	{
		delete[] version_;
		version_ = nullptr;
	}
	if (url_ != nullptr)
	{
		delete[] url_;
		url_ = nullptr;
	}
	if (response_ != nullptr)
	{
		delete[] response_;
		response_ = nullptr;
	}
}

void HttpServer::Init(int fd_, sockaddr_in addr)
{
	this->fd_ = fd_;
	this->addr_ = addr;
	this->code_ = OK;
	++usernum_;

	this->buf_ = new char[BUF_SIZE];
	memset(buf_, 0, BUF_SIZE);
	this->prebuf_ = this->buf_;

	this->version_ = new char[SAVE_SIZE];
	memset(version_, 0, SAVE_SIZE);

	this->url_ = new char[SAVE_SIZE];
	memset(url_, 0, SAVE_SIZE);

	this->host_ = new char[SAVE_SIZE];
	memset(host_, 0, SAVE_SIZE);
}

void HttpServer::CloseConnect()
{
	if (fd_ != -1)
	{
		printf("close connect fd : %d ,ip : %d\n", fd_, addr_.sin_addr.s_addr);
		epoll_ctl(epollfd_, EPOLL_CTL_DEL, fd_, NULL);
		--usernum_;
		fd_ = -1;
	}
}

bool HttpServer::Read()
{
	memset(buf_, 0, BUF_SIZE);
	int n = 0, readnum = 0;
	while ((readnum = recv(fd_, buf_ + n, BUF_SIZE, MSG_DONTWAIT)) > 0)
	{
		n += readnum;
	}
	if (readnum == -1 && errno != EAGAIN)
	{
		perror("read error");
		CloseConnect();
		return false;
	}
	else if (n == 0)
	{
		CloseConnect();
		return true;
	}
	printf("收到新的请求:\n%s\n", buf_);
	return true;
}

bool HttpServer::Write()
{
	//发送内容
	int writenum = 0;
	int written = 0;
	int len = strlen(response_);
	while (writenum = send(fd_, response_ + written, len - written, 0) > 0)
	{
		written += writenum;
		if (writenum == -1 && errno != EAGAIN)
		{
			perror("write error");
			CloseConnect();
			return false;
		}
	}
	return true;
}

bool HttpServer::Process()
{
	//解析
	if (Parse() == false)
	{
		CloseConnect();
		return false;
	}

	//生成响应
	response_ = new char[BUF_SIZE];
	memset(response_, 0, BUF_SIZE);
	if (GenResponse(response_) == false)
	{
		//生成失败
		CloseConnect();
		return false;
	}
	// printf("生成响应:\n%s\n\n\n", response_);

	//添加epollout,等待缓冲区能写
	epoll_event ev;
	ev.events = EPOLLOUT | EPOLLET;
	ev.data.fd = fd_;
	epoll_ctl(epollfd_, EPOLL_CTL_MOD, fd_, &ev);
	return true;
}

// GetLine返回nullptr后prebuf为空或只有请求数据
const char *HttpServer::GetLine()
{
	if (buf_ == nullptr)
	{
		return nullptr;
	}

	int len = strlen(prebuf_);
	for (int i = 0; i < len; ++i)
	{
		if (prebuf_[i] == '\r' && i + 1 <= len && prebuf_[++i] == '\n')
		{
			char temp[BUF_SIZE];
			memset(temp, 0, BUF_SIZE);
			stpncpy(temp, prebuf_, i - 1);
			prebuf_ += i + 1;
			if (prebuf_[0] == '\r' && prebuf_[1] == '\n')
			{
				prebuf_ += 2;
				return "\r\n";
			}
			const char *p = temp;
			return p;
		}
	}
	return nullptr;
}

HttpServer::Code HttpServer::ParseLine(const char *line)
{
	////解析GET
	char word[SAVE_SIZE];
	memset(word, 0, SAVE_SIZE);
	int n = strcspn(line, " ");
	stpncpy(word, line, n);
	if (strcmp(word, "GET") == 0)
	{
		method_ = Get;
		line += n + 1;
	}
	else if (strcmp(word, "POST") == 0)
	{
		method_ = Post;
		line += n + 1;
	}
	else
	{
		code_ = BadRequest;
		return code_;
	}

	//解析url
	memset(word, 0, SAVE_SIZE);
	n = strcspn(line, " ");
	stpncpy(word, line, n);
	if (strncmp(word, "/", 1) == 0)
	{
		strcpy(url_, word);
		if (url_ == nullptr)
		{
			//有错
		}
		else if (strcmp(url_, "/") == 0)
		{
			strcpy(url_, "/index.html");
		}
		line += n + 1;
	}
	else
	{
		code_ = BadRequest;
		return code_;
	}

	//解析版本
	memset(word, 0, SAVE_SIZE);
	n = strcspn(line, " ");
	stpncpy(word, line, n);
	if (strcmp(word, "HTTP/1.1") == 0)
	{
		strcpy(version_, word);
		line += n + 1;
	}
	else
	{
		code_ = BadRequest;
		return code_;
	}
	return code_;
}

HttpServer::Code HttpServer::ParseHeader(const char *header)
{
	////解析GET
	char word[SAVE_SIZE];
	memset(word, 0, SAVE_SIZE);
	int n = strcspn(header, " ");
	stpncpy(word, header, n);
	if (strcmp(word, "Host:") == 0)
	{
		header += n + 1;
		n = strcspn(header, " ");
		strcpy(host_, header);
		return OK;
	}
	// else if (...)  //其他头部
	//{
	//
	//
	// }
	// else			 //出错
	//{
	//	code_ = BadRequest;
	//	return code_;
	// }
	return OK; // 没写完，暂时OK
}

bool HttpServer::Parse()
{
	const char *line = GetLine();
	if (line == nullptr)
	{
		code_ = BadRequest;
		// todo

		return false;
	}
	//解析请求行
	if (ParseLine(line) != OK)
	{
		// todo
		// badrequest
		return false;
	}
	//解析请求头
	line = GetLine();
	while (line != nullptr)
	{
		if (strcmp(line, "\r\n") == 0)
		{
			//解析来是请求数据
			break;
		}
		else if (ParseHeader(line) == OK)
		{
			line = GetLine();
		}
		else
		{
			// todo
			// badrequest
			return false;
		}
	}
	if (line == nullptr)
	{
		code_ = BadRequest;
		return false;
	}

	return true;
}

bool HttpServer::GenResponse(char *response_)
{
	if (code_ == OK)
	{
		//拼接路径
		path_ = new char[SAVE_SIZE];
		strcpy(path_, ROOT_PATH);
		strcat(path_, url_);
		printf("请求资源 : %s\n\n\n\n",path_);
		//添加Content-Length
		struct stat filestat;
		if (stat(path_, &filestat) != 0)
		{
			code_ = NotFound;
			sprintf(response_, "%s %d Not Found\nContent-Length: 0\n\n", version_, code_);
			perror("stat");
			return true;
		}
		//首行
		sprintf(response_, "%s %d OK\n", version_, code_);
		char temp[SAVE_SIZE];
		//添加Content-Type
		memset(temp, 0, SAVE_SIZE);
		sprintf(temp, "Content-Type: text/html; charset=UTF-8\n");
		strcat(response_, temp);
		//添加长度
		memset(temp, 0, SAVE_SIZE);
		sprintf(temp, "Content-Length: %ld\n", filestat.st_size);
		strcat(response_, temp);
		//结束头部
		memset(temp, 0, SAVE_SIZE);
		sprintf(temp, "\n");
		strcat(response_, temp);
		//读取文件数据
		FILE *file = fopen(path_, "r");
		if (file == nullptr)
		{
			perror("file open");
			return false;
		}
		memset(temp, 0, SAVE_SIZE);
		while (fgets(temp, SAVE_SIZE, file) != nullptr)
		{
			strcat(response_, temp);
			memset(temp, 0, SAVE_SIZE);
		}
		fclose(file);
		delete[] path_;
		path_ = nullptr;
		return true;
	}
	return false;
}
