#include <iostream>
#include <chrono>
#include <functional>
#include <memory>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/lock_types.hpp>


class thread_pool;
template <class R> class future;

template <class R> using task_type = std::function<R()>;

enum class TaskState
{
    IDLE,
    RUNNING,
    COMPLETE
};

class base_state
{
public:
    virtual void wait_for_result() = 0;
    virtual void do_work() = 0;
};

template<class R>
class state : public base_state
{
    friend class future<R>;
public:
    state(task_type<R> task) : task_(task), state_(TaskState::IDLE)
    {
    }

    virtual void do_work()
    {
        state_ = TaskState::RUNNING;
        result_ = task_();
        state_ = TaskState::COMPLETE;
        condition_.notify_all();
    }

    void wait_for_result()
    {
        if (state_ == TaskState::RUNNING)
        {
            boost::unique_lock<boost::mutex> guard(mutex_);
            condition_.wait(guard, [&]{return state_ == TaskState::COMPLETE;});
        }
    }

private:
    TaskState state_;
    task_type<R> task_;
    R result_;
    boost::condition_variable condition_;
    boost::mutex mutex_;
};

template <>
class state<void> : public base_state
{
    friend class future<void>;
public:
    state(task_type<void> task)
      : task_(task), state_(TaskState::IDLE) {}

    virtual void do_work()
    {
        state_ = TaskState::RUNNING;
        task_();
        state_ = TaskState::COMPLETE;
        condition_.notify_all();
    }

    void wait_for_result()
    {
        if (state_ == TaskState::RUNNING)
        {
            boost::unique_lock<boost::mutex> guard(mutex_);
            condition_.wait(guard, [&]{return state_ == TaskState::COMPLETE;});
        }
    }
private:
    TaskState state_;
    task_type<void> task_;
    boost::condition_variable condition_;
    boost::mutex mutex_;
};

template <class T>
class pcqueue
{
public:
    void push(T&& item)
    {
        boost::lock_guard<boost::mutex> guard(mutex_);
        queue_.push(std::forward<T>(item));
        condition_.notify_one();
    }

    T pop()
    {
        boost::unique_lock<boost::mutex> guard(mutex_);
        condition_.wait(guard, [&]{ return queue_.empty(); });
        T result(std::move(queue_.front()));
        queue_.pop();
        return result;
    }

private:
    boost::mutex mutex_;
    boost::condition_variable condition_;
    std::queue<T> queue_;
};

template <class R>
class future_base
{
public:
    future_base(std::shared_ptr<thread_pool> pool, std::shared_ptr<state<R>> future_state)
        : state_(future_state), pool_(pool)
    {
    }

    future_base(future_base&& other)
    {
        std::swap(state_, other.state_);
        std::swap(pool_, other.pool_);
    }

protected:
    std::shared_ptr<state<R>> state_;
    std::weak_ptr<thread_pool> pool_;
};

template <class R>
class future : public future_base<R>
{
    typedef future_base<R> super;
public:
    future(std::shared_ptr<thread_pool> pool, std::shared_ptr<state<R>> future_state)
      : future_base<R>(pool, future_state)
    {
    }

    future(future&& other) : future_base<R>(std::forward<future_base<R>>(other))
    {
    }

    R get()
    {
        super::state_->wait_for_result();
        return super::state_->result_;
    }

    void wait()
    {
    }

    void cancel()
    {
    }
};

template <>
class future<void> : public future_base<void>
{
    typedef future_base<void> super;
public:
    future(std::shared_ptr<thread_pool> pool, std::shared_ptr<state<void>> future_state)
      : future_base<void>(pool, future_state)
    {
    }

    future(future&& other) : future_base<void>(std::forward<future_base<void>>(other))
    {
    }


    void get()
    {
        super::state_->wait_for_result();
    }

    void wait()
    {
    }

    void cancel()
    {
    }
};



class thread_pool : public std::enable_shared_from_this<thread_pool>
{
public:
    static std::shared_ptr<thread_pool> instance(int num)
    {
        static auto pool = std::make_shared<thread_pool>(num);
        return pool;
    }

    template <class Fn, class... Args>
    future<typename std::result_of<Fn(Args...)>::type>
    submit(Fn func, Args... args)
    {
        typedef typename std::result_of<Fn(Args...)>::type result_type;
        task_type<result_type> task = std::bind(func, args...);

        auto shared_state = std::make_shared<state<result_type>>(task);
        task_queue_.push(shared_state);

        return future<result_type>(shared_from_this(), shared_state);
    }

    thread_pool(uint thread_num)
      : thread_count_(thread_num)
    {
        for (uint i = 0; i < thread_num; ++i)
        {
            threads_.emplace_back(boost::thread(std::bind(&thread_pool::thread_routine, this)));
        }
    }

    void start()
    {
        construction_condition_.notify_all();
    }

    uint thread_count_;
    std::vector<boost::thread> threads_;
    pcqueue<std::shared_ptr<base_state>> task_queue_;
    boost::mutex construction_mutex_;
    boost::condition_variable construction_condition_;

    void thread_routine()
    {
        {
            boost::unique_lock<boost::mutex> guard(construction_mutex_);
            construction_condition_.wait(guard, [&]{return threads_.size() == thread_count_;});
        }

        while (true)
        {
            try
            {
                boost::this_thread::interruption_point();
                std::shared_ptr<base_state> task_state = task_queue_.pop();
                task_state->do_work();
            }
            catch (const boost::thread_interrupted& e)
            {
            }
        }
    }
};

int main()
{
    auto pool = thread_pool::instance(2);
    pool->start();

    auto task = [](int wait) {
      std::cerr << std::this_thread::get_id() << " sleeping!" << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(wait));
      std::cerr << std::this_thread::get_id() << " woke up!" << std::endl;
    };

    std::vector<future<void>> vec;
    for (int i = 0; i < 10; ++i)
    {
        vec.emplace_back(pool->submit(task, i));
    }

    for (auto&& fut : vec)
        fut.get();
    return 0;
}