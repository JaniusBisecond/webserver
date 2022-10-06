#pragma once

#include <queue>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include "httpserver.h"

class ThreadPool
{
public:
	ThreadPool(int queue_size = 1024, int thread_num = 8);
	~ThreadPool();
	int AddTask(HttpServer* httpserver);
	int Destory();

private:
	static void* Work(void* arg);//为什么static

private:
	std::queue<HttpServer*>* server_queue;
	std::vector<pthread_t>* threads;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int queue_size;
	int thread_num;
	bool run;
};

