#include "threadpool.h"

ThreadPool::ThreadPool(int queue_size, int thread_num):queue_size(queue_size),thread_num(thread_num),run(true)
{
	if (queue_size < 0 || thread_num < 0)
	{
		printf("无效初始化参数\n");
		return;
	}

	if (pthread_mutex_init(&mutex, NULL) != 0)
	{
		printf("互斥锁初始化失败\n");
		return;
	}
	if (pthread_cond_init(&cond, NULL) != 0)
	{
		printf("条件锁初始化失败\n");
		return;
	}
	server_queue = new std::queue<HttpServer*>;
	threads = new std::vector<pthread_t>(thread_num);
	if (threads == nullptr)
	{
		printf("线程池创建失败\n");
		return;
	}
	for (int i = 0; i < thread_num; ++i)
	{
		// printf("创建第%d个线程\n", i + 1);
		if (pthread_create(&threads->at(i), NULL, Work, this) != 0)
		{
			delete[] threads;
			printf("第%d个线程创建失败\n", i + 1);
			return;
		}
	}
}

ThreadPool::~ThreadPool()
{
	if (run == true)
	{
		Destory();
	}
	delete threads;//为什么不是delete[]
	threads = nullptr;
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
	while (!server_queue->empty())
	{
		server_queue->pop();
	}
	delete server_queue;
}

int ThreadPool::AddTask(HttpServer* httpserver)
{
	pthread_mutex_lock(&mutex);
	if (server_queue->size() >= queue_size)
	{
		pthread_mutex_unlock(&mutex);
		return -1;
	}
	server_queue->push(httpserver);
	pthread_mutex_unlock(&mutex);
	pthread_cond_signal(&cond);
	return 0;
}

int ThreadPool::Destory()
{
	run = false;
	//拿到锁防止其他线程继续操作
	if (pthread_mutex_lock(&mutex) != 0)
	{
		return -1;
	}
	//通知其他线程进行收尾
	if (pthread_cond_broadcast(&cond) != 0 || pthread_mutex_unlock(&mutex) != 0)
	{
		return -1;
	}
	//等待收尾
	for (int i = 0; i < thread_num; ++i) 
	{
		if (pthread_join(threads->at(i), NULL) != 0)
		{
			return -2;
		}
	}
	printf("收尾完毕！\n");
	return 0;
}

void* ThreadPool::Work(void* arg)
{
	ThreadPool* threadpool = (ThreadPool*)arg;
	while(1)
	{
		pthread_mutex_lock(&threadpool->mutex);
		while (threadpool->server_queue->empty()|| threadpool->run == false)//虚假唤醒
		{
			if (threadpool->run == false)
			{
				pthread_mutex_unlock(&threadpool->mutex);
				printf("子线程结束\n");
				return nullptr;
			}
			pthread_cond_wait(&threadpool->cond, &threadpool->mutex);//先mutex lock 再 cond wait
		}
		HttpServer* server = threadpool->server_queue->front();
		threadpool->server_queue->pop();
		pthread_mutex_unlock(&threadpool->mutex);
		server->Process();
	}
	return nullptr;
}

