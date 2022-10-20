#include "httpserver.h"

int HttpServer::epollfd_ = -1;
int HttpServer::usernum_ = 0;

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

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
	this->keepalive_ = false;
	this->fileaddress_ = nullptr;

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
	this->code_ = rhs.code_;
	this->linestate_ = rhs.linestate_;
	this->keepalive_ = rhs.keepalive_;
	this->fileaddress_ = rhs.fileaddress_;

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
	this->code_ = Parseing;
	this->linestate_ = CHECK_STATE_REQUESTLINE;
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
		if (buf_)
		{
			delete[] buf_;
			buf_ = nullptr;
		}
		if (response_)
		{
			delete[] response_;
			response_ = nullptr;
		}

		epoll_ctl(epollfd_, EPOLL_CTL_DEL, fd_, NULL);
		--usernum_;
		fd_ = -1;
		close(fd_);
	}
}

bool HttpServer::Read()
{
	if (buf_ == nullptr)
	{
		this->buf_ = new char[BUF_SIZE];
		memset(buf_, 0, BUF_SIZE);
		this->prebuf_ = this->buf_;
	}
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

	return true;
}

bool HttpServer::Write()
{
	int havesend = 0;
	int send = 0;
	while (1)
	{
		send = writev(fd_, iov_, iov_count_);
		if (send < 0)
		{
			delete[] response_;
			response_ = nullptr;
			if (errno == EAGAIN)
			{
				modfd(epollfd_, fd_, EPOLLOUT);
				return true;
			}
			if (fileaddress_)
			{
				munmap(fileaddress_, filestat_.st_size);
				fileaddress_ = nullptr;
			}
			return false;
		}
		havesend += send;
		if (havesend >= sendnum_)
		{
			if (fileaddress_)
			{
				munmap(fileaddress_, filestat_.st_size);
				fileaddress_ = nullptr;
			}
			modfd(epollfd_, fd_, EPOLLIN);
			delete[] response_;
			response_ = nullptr;
			return true;
		}
		else if (havesend < iov_[0].iov_len)
		{
			iov_[0].iov_base = response_ + havesend;
			iov_[0].iov_len = iov_[0].iov_len - havesend;
		}
		else
		{
			iov_[1].iov_base = fileaddress_ + havesend - response_idx_;
			iov_[1].iov_len = sendnum_ - havesend;
			iov_[0].iov_len = 0;
		}
	}
}

bool HttpServer::Process()
{
	//解析
	Parse();

	//生成响应
	response_ = new char[BUF_SIZE];
	memset(response_, 0, BUF_SIZE);
	response_idx_ = 0;
	if (GenResponse(response_) == false)
	{
		//生成失败
		CloseConnect();
		return false;
	}

	//添加epollout,等待缓冲区能写
	epoll_event ev;
	ev.events = EPOLLOUT | EPOLLET | EPOLLIN;
	ev.data.fd = fd_;
	epoll_ctl(epollfd_, EPOLL_CTL_MOD, fd_, &ev);
	// printf("正确的请求报文！\n");
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

HttpServer::Code HttpServer::ParseRequestLine(const char *line)
{

	////解析GET/Post
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
			code_ = BadRequest;
		}
		else if (strcmp(url_, "/") == 0)
		{
			strcpy(url_, "/index.html");
		}
		line += n + 1;
		path_ = new char[SAVE_SIZE];
		strcpy(path_, ROOT_PATH);
		strcat(path_, url_);
		if (stat(path_, &filestat_) != 0)
		{
			code_ = NotFound;
		}
		else
		{
			int filefd = open(path_, O_RDONLY);
			fileaddress_ = (char *)mmap(0, filestat_.st_size, PROT_READ, MAP_PRIVATE, filefd, 0); //记得mummap
			close(filefd);
		}
	}
	else
	{
		code_ = BadRequest;
	}

	//解析版本
	memset(word, 0, SAVE_SIZE);
	n = strcspn(line, " ");
	stpncpy(word, line, n);
	if (strcmp(word, "HTTP/1.1") == 0 || strcmp(word, "HTTP/1.0") == 0)
	{
		strcpy(version_, word);
		line += n + 1;
	}
	else
	{
		code_ = BadRequest;
	}
	linestate_ = CHECK_STATE_HEADER;
	return code_;
}

HttpServer::Code HttpServer::ParseHeader(const char *header)
{
	if (strcmp(header, "\r\n") == 0)
	{
		linestate_ = CHECK_STATE_CONTENT;
		code_ = OK;
		return code_;
	}
	//开始解析
	char word[SAVE_SIZE];
	memset(word, 0, SAVE_SIZE);
	int n = strcspn(header, " ");
	stpncpy(word, header, n);

	if (strcmp(word, "Host:") == 0)
	{
		header += n + 1;
		strcpy(host_, header);
		code_ = Parseing;
		return code_;
	}
	else if (strcmp(word, "Connection:") == 0)
	{
		header += n + 1;
		if (strcmp(header, "keep-alive") == 0)
		{
			keepalive_ = true;
		}
		else
		{
			keepalive_ = false;
		}
		code_ = Parseing;
		return code_;
	}
	// more else if

	//
	else
	{
		// unknown header
	}
	return code_;
}

HttpServer::Code HttpServer::Parse()
{
	const char *line;
	while (line = GetLine())
	{
		switch (linestate_)
		{
		case CHECK_STATE_REQUESTLINE:
		{
			if (ParseRequestLine(line) == BadRequest)
			{
				return code_;
			}
			else if (code_ == NotFound)
			{
				return code_;
			}
			break;
		}
		case CHECK_STATE_HEADER:
		{
			if (ParseHeader(line) == BadRequest)
			{
				return code_;
			}
			else if (code_ == OK)
			{
			}
			break;
		}
		case CHECK_STATE_CONTENT:
		{

			break;
		}

		default:
			break;
		}
	}
	return code_;
}

bool HttpServer::GenResponse(char *response_)
{
	switch (code_)
	{
	case BadRequest:
	{
		AddStatusLine(BadRequest, error_400_title);
		AddHeaders(strlen(error_400_form));
		return AddContent(error_400_form);
	}
	case NotFound:
	{
		AddStatusLine(NotFound, error_404_title);
		AddHeaders(strlen(error_404_form));
		return AddContent(error_404_form);
	}
	case OK:
	{
		AddStatusLine(OK, ok_200_title);
		AddHeaders(filestat_.st_size);
		iov_[0].iov_base = response_; //头部
		iov_[0].iov_len = response_idx_;
		iov_[1].iov_base = fileaddress_; //数据
		iov_[1].iov_len = filestat_.st_size;
		iov_count_ = 2;
		sendnum_ = response_idx_ + filestat_.st_size;
		return true;
	}
	default:
		return false;
	}

	iov_[0].iov_base = response_; //头部
	iov_[0].iov_len = response_idx_;
	iov_count_ = 1;
	sendnum_ = response_idx_;
	return true;
}

template <typename T>
const T &HttpServer::VaArg(const T &t)
{
	return t;
}
const char *HttpServer::VaArg(const char *&str)
{
	return str;
}
template <typename... Args>
bool HttpServer::AddResponse(const char *format, const Args &...rest)
{
	response_idx_ += sprintf(response_ + response_idx_, format, VaArg(rest)...);
	return true;
}

bool HttpServer::AddStatusLine(Code code, const char *info)
{
	return AddResponse("%s %d %s\r\n", version_, code_, info);
}

bool HttpServer::AddHeaders(const size_t &filesize)
{
	return AddContentLength(filesize) && AddContentType() && AddLinger() && AddBlankLine();
}

bool HttpServer::AddContentLength(const size_t &filesize)
{
	return AddResponse("Content-Length: %ld\r\n", filesize);
}

bool HttpServer::AddContentType()
{
	return AddResponse("%s", "Content-Type: text/html; charset=UTF-8\r\n");
}

bool HttpServer::AddBlankLine()
{
	return AddResponse("%s", "\r\n");
}

bool HttpServer::AddLinger()
{
	return AddResponse("Connection: %s\r\n", (keepalive_ == true) ? "keep-alive" : "Close");
}

bool HttpServer::AddContent(const char *str)
{
	return AddResponse("%s", str);
}

void HttpServer::modfd(int epollfd, int fd, int ev)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}