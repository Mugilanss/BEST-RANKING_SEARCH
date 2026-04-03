#pragma once
#include <vector>
#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>

class ThreadPool {
public:
    explicit ThreadPool(size_t n) : stop(false) {
        workers.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lk(mu);
                        cv.wait(lk, [this] {
                            return stop || !tasks.empty();
                        });

                        if (stop && tasks.empty())
                            return;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    // Modern C++ replacement for result_of:
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lk(mu);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task]() { (*task)(); });
        }

        cv.notify_one();
        return res;
    }

    void shutdown() {
        {
            std::unique_lock<std::mutex> lk(mu);
            stop = true;
        }

        cv.notify_all();

        for (auto &t : workers)
            if (t.joinable())
                t.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex mu;
    std::condition_variable cv;

    bool stop;
};
