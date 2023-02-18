#ifndef TH
#define TH


#include<mutex>
#include<vector>
#include<thread>
#include<condition_variable>
#include<atomic>
#include<future>
#include<queue>
 
const int MAX_THREAD_SIZE = 1024;	// 默认线程最大数量
const int MAX_TASK_SIZE = 1024;		// 默认任务的最大数量

namespace TianHui
{
	template<typename T>
	class mLambda
	{
		mLambda() = delete;
		~mLambda() = delete;

	};

	// 接收lambda表达式
	template<typename Re, typename ...Args>
	class mLambda<Re(Args...)>
	{
	public:

		template<typename T>
		explicit mLambda<Re(Args...)>(T&& lambda) :
			lambda_ptr(static_cast<void*>(new T(std::forward<T>(lambda)))),
			call_(Save<T>::call),
			dell_(Save<T>::del)
		{}

		mLambda<Re(Args...)>(const mLambda<Re(Args...)>& p) = delete;

		mLambda<Re(Args...)>( mLambda<Re(Args...)> && p)noexcept :
			lambda_ptr(p.lambda_ptr),
			call_(p.call_),
			dell_(p.dell_)
		{
			p.lambda_ptr = nullptr;
			p.call_ = nullptr;
			p.dell_ = nullptr;
		}

		mLambda<Re(Args...)>& operator=(const mLambda<Re(Args...)>&) = delete;
		mLambda<Re(Args...)>& operator=(mLambda<Re(Args...)>&& p) = delete;

		auto operator()(Args...args)
		{
			return call_(lambda_ptr, std::forward<Args>(args)...);
		}

		~mLambda<Re(Args...)>()
		{
			if (lambda_ptr)
				dell_(lambda_ptr);
			lambda_ptr = nullptr;
			call_ = nullptr;
			dell_ = nullptr;
		}
	public:
		template<typename H>
		class Save
		{
		public:
			static Re call(const void* (&ptr), Args...args)
			{
				return ((H*)ptr)->operator()(std::forward<Args>(args)...);
				//return (*(H*)ptr)(std::forward<Args>(args)...);
			}

			static void del(const void* (&ptr))
			{
				if (ptr)
					delete(H*)ptr;
				ptr = nullptr;
			}

		};
	private:
		const void* lambda_ptr;
		Re(*call_)(const void* (&), Args...);
		void(*dell_)(const void* (&));
	};

}

enum class Mode
{
	FIXED,
	CATCH
};




class ThreadPool
{

public:

	// 创建线程池
	ThreadPool():
		maxThreadSize_(MAX_THREAD_SIZE),
		maxTaskSize_(MAX_TASK_SIZE),
		taskSize_(0),
		threadSize_(0),
		isEnd_(false),
		mode_(Mode::FIXED) 
	{}								

	// 销毁线程池
	~ThreadPool()
	{
		isEnd_ = true;

		{			
			std::unique_lock<std::mutex>lock(mutexPool_);
			taskSize_ = MAX_TASK_SIZE;
			notEmpty_.notify_all();
			
			endPool_.wait(lock, [this]()->bool {return threadSize_ <= 0; });		

		}

		int n = static_cast<int>(threadPool_.size());
		if (n > 0)
			for (int i = 0; i < n; ++i)
			{
				auto& temp = threadPool_[i];
				if (temp)
					delete temp;
				temp = nullptr;
			}
		threadPool_.clear();

	};						

	// 设置线程池里线程的最大的数量
	void setThreadSize(int size);		

	// 设置任务队列里任务的最大数量
	void setTaskSize(int size);

	// 设置线程池的模式
	void setPoolMode(Mode mode);		

	template<typename _Ty, typename ..._list>
	inline auto submit(_Ty(&& _callback), _list && ...t)->std::future< decltype(_callback(t...)) >;

	// 启动线程池并设置启动的线程个数
	void start(int size = 4)
	{
		for (int i = 0; i < size; ++i, ++threadSize_)
		{
			threadPool_.emplace_back(new Thread(this));
		}

		for (int i = 0; i < size; ++i)
			threadPool_[i]->star();

	}

private:

	// 线程封装
	class Thread
	{
	public:

		Thread(ThreadPool* th) :
			this_(th)
		{}

		~Thread() {};

		void star()
		{
			std::thread t(&ThreadPool::taskFunction, this_);
			t.detach();

			this_ = nullptr;
		}


	private:
		
		ThreadPool* this_;
	};
	


	// 判断线程池是否结束
	bool isEnding()
	{
		return isEnd_;
	}

	// 处理任务列表
	void taskFunction()
	{

		for (;!isEnding();)
		{

			if (isEnding())
				break;

			std::unique_lock<std::mutex>lock(mutexPool_);
			notEmpty_.wait(lock, [this]()->bool {return taskSize_ > 0; });
			if (isEnding() && taskQueue_.size() <= 0)
				break;

			--taskSize_;

			auto& temp = taskQueue_.front();
			temp();

			notFull_.notify_all();

			taskQueue_.pop();			
			
			if (taskSize_ > 0)
				notEmpty_.notify_all();

		}

		--threadSize_;

		endPool_.notify_all();
		std::cout <<std::this_thread::get_id()<<" :" << "thread exit!" << std::endl;
		return;
	}

private:
	// 线程池锁
	std::mutex mutexPool_;	

	// 线程池里线程的最大的数量
	int maxThreadSize_;		

	// 任务队列里任务的最大的数量
	int maxTaskSize_;

	// 当前任务数量
	std:: atomic_int taskSize_;

	// 当前线程数量
	std:: atomic_int threadSize_;

	// 线程工作模式
	Mode mode_;			

	// 任务队列
	std::queue<TianHui::mLambda<void()> >taskQueue_;	

	// 线程池及其序号
	std::vector<Thread*> threadPool_;

	// 任务队列不空信号量
	std::condition_variable notEmpty_;	

	// 任务队列不满信号量
	std::condition_variable notFull_;	

	// 线程池结束
	std::condition_variable endPool_;

	// 判断线程是否结束
	std::atomic_bool isEnd_;
};



template<typename _Ty, typename ..._list>
inline auto ThreadPool::submit(_Ty(&& _callback), _list && ...t)->std::future< decltype(_callback(t...)) >
{
	using Rtype = decltype(_callback(t...));
	//auto time = std::chrono::high_resolution_clock::now();

	auto task = new std::packaged_task< Rtype()>([_callback, t...]()->Rtype {return _callback(t...); });

	{
		std::unique_lock<std::mutex>lock(mutexPool_);
		if (! notFull_.wait_for(lock, std::chrono::seconds(1), [this]()->bool {return taskSize_ < maxTaskSize_; }))
		{
			std::cerr << "任务队列已满,任务提交失败！" << std::endl;
			throw "任务队列已满,任务提交失败！";
		}

		taskQueue_.emplace(TianHui::mLambda<void()>(
			[task]()->void
			{ 
				(*task)();
				if (task)delete task; 
			}));
	}

	++taskSize_;
	notEmpty_.notify_all();
	

	return task->get_future();

}




#endif // !TH
