#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <boost/thread.hpp>

enum class TaskType {
    Factorial,
    Fibonacci,
    SumDigits,
    IsPrime,
    Gcd,
    ReverseNumber
};

struct Task {
    int id = 0;
    TaskType type = TaskType::Factorial;
    std::int64_t a = 0;
    std::int64_t b = 0;
    int priority = 0;
};

std::uint64_t factorial(std::uint64_t n) {
    std::uint64_t result = 1;
    for (std::uint64_t i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

std::uint64_t fibonacci(std::uint64_t n) {
    if (n == 0) {
        return 0;
    }
    if (n == 1) {
        return 1;
    }
    std::uint64_t a = 0;
    std::uint64_t b = 1;
    for (std::uint64_t i = 2; i <= n; ++i) {
        const std::uint64_t next = a + b;
        a = b;
        b = next;
    }
    return b;
}

std::int64_t sumDigits(std::int64_t n) {
    n = std::llabs(n);
    std::int64_t sum = 0;
    while (n > 0) {
        sum += (n % 10);
        n /= 10;
    }
    return sum;
}

bool isPrime(std::int64_t n) {
    if (n < 2) {
        return false;
    }
    if (n % 2 == 0) {
        return n == 2;
    }
    for (std::int64_t d = 3; d * d <= n; d += 2) {
        if (n % d == 0) {
            return false;
        }
    }
    return true;
}

std::int64_t gcdValue(std::int64_t a, std::int64_t b) {
    a = std::llabs(a);
    b = std::llabs(b);
    while (b != 0) {
        const std::int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

std::int64_t reverseNumber(std::int64_t n) {
    const bool neg = n < 0;
    n = std::llabs(n);
    std::int64_t rev = 0;
    while (n > 0) {
        rev = rev * 10 + (n % 10);
        n /= 10;
    }
    return neg ? -rev : rev;
}

std::string executeTask(const Task& task) {
    switch (task.type) {
        case TaskType::Factorial: {
            const auto n = static_cast<std::uint64_t>(task.a);
            return "Факториал(" + std::to_string(n) + ") = " + std::to_string(factorial(n));
        }
        case TaskType::Fibonacci: {
            const auto n = static_cast<std::uint64_t>(task.a);
            return "Фибоначчи(" + std::to_string(n) + ") = " + std::to_string(fibonacci(n));
        }
        case TaskType::SumDigits:
            return "Сумма цифр(" + std::to_string(task.a) + ") = " + std::to_string(sumDigits(task.a));
        case TaskType::IsPrime:
            return "Число " + std::to_string(task.a) + (isPrime(task.a) ? " простое" : " не простое");
        case TaskType::Gcd:
            return "НОД(" + std::to_string(task.a) + ", " + std::to_string(task.b) + ") = " + std::to_string(gcdValue(task.a, task.b));
        case TaskType::ReverseNumber:
            return "Реверс(" + std::to_string(task.a) + ") = " + std::to_string(reverseNumber(task.a));
    }
    return "Неизвестная задача";
}

Task makeRandomTask(int id, std::mt19937& rng) {
    std::uniform_int_distribution<int> typeDist(0, 5);
    std::uniform_int_distribution<int> smallDist(5, 30);
    std::uniform_int_distribution<int> mediumDist(50, 5000);
    std::uniform_int_distribution<int> priorityDist(1, 10);

    Task task;
    task.id = id;
    task.priority = priorityDist(rng);
    task.type = static_cast<TaskType>(typeDist(rng));

    switch (task.type) {
        case TaskType::Factorial:
            task.a = smallDist(rng) % 21;
            break;
        case TaskType::Fibonacci:
            task.a = smallDist(rng) + 10;
            break;
        case TaskType::SumDigits:
            task.a = mediumDist(rng) * 1001;
            break;
        case TaskType::IsPrime:
            task.a = mediumDist(rng);
            break;
        case TaskType::Gcd:
            task.a = mediumDist(rng);
            task.b = mediumDist(rng);
            break;
        case TaskType::ReverseNumber:
            task.a = mediumDist(rng) * 111;
            break;
    }

    return task;
}

class ThreadPool {
public:
    explicit ThreadPool(int workerCount) {
        workers_.reserve(workerCount);
        for (int i = 0; i < workerCount; ++i) {
            workers_.emplace_back(&ThreadPool::workerLoop, this);
        }
    }

    ~ThreadPool() {
        closeSubmission();
        for (auto& t : workers_) {
            t.join();
        }
    }

    std::future<std::string> enqueue(Task task) {
        auto item = std::make_shared<WorkItem>();
        item->priority = task.priority;
        item->task = std::move(task);
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (!accepting_) {
                throw std::runtime_error("Прием задач закрыт");
            }
            item->seq = sequence_++;
            auto future = item->promise.get_future();
            queue_.push(std::move(item));
            cv_.notify_one();
            return future;
        }
    }

    void closeSubmission() {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            accepting_ = false;
        }
        cv_.notify_all();
    }

    void waitUntilDone(int totalTasks) {
        std::unique_lock<std::mutex> lock(doneMutex_);
        doneCv_.wait(lock, [&]() { return completed_ >= totalTasks; });
    }

    int completed() const {
        std::lock_guard<std::mutex> lock(doneMutex_);
        return completed_;
    }

private:
    struct WorkItem {
        int priority = 0;
        int seq = 0;
        Task task;
        std::promise<std::string> promise;
    };

    struct WorkItemCmp {
        bool operator()(const std::shared_ptr<WorkItem>& lhs, const std::shared_ptr<WorkItem>& rhs) const {
            if (lhs->priority == rhs->priority) {
                return lhs->seq > rhs->seq;
            }
            return lhs->priority < rhs->priority;
        }
    };

    void workerLoop() {
        while (true) {
            std::shared_ptr<WorkItem> item;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                cv_.wait(lock, [&]() { return !queue_.empty() || !accepting_; });
                if (queue_.empty() && !accepting_) {
                    return;
                }
                item = queue_.top();
                queue_.pop();
            }

            auto taskFuture = std::async(std::launch::async, [task = item->task]() {
                return executeTask(task);
            });
            item->promise.set_value(taskFuture.get());

            {
                std::lock_guard<std::mutex> lock(doneMutex_);
                ++completed_;
            }
            doneCv_.notify_one();
        }
    }

    mutable std::mutex queueMutex_;
    std::condition_variable cv_;
    std::priority_queue<std::shared_ptr<WorkItem>, std::vector<std::shared_ptr<WorkItem>>, WorkItemCmp> queue_;
    bool accepting_ = true;
    int sequence_ = 0;

    mutable std::mutex doneMutex_;
    std::condition_variable doneCv_;
    int completed_ = 0;

    std::vector<boost::thread> workers_;
};

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::vector<Task> tasks;
    tasks.push_back({1, TaskType::Factorial, 10, 0, 10});
    tasks.push_back({2, TaskType::Fibonacci, 40, 0, 8});
    tasks.push_back({3, TaskType::SumDigits, 123456789, 0, 5});
    tasks.push_back({4, TaskType::IsPrime, 9973, 0, 9});
    tasks.push_back({5, TaskType::Gcd, 252, 198, 7});
    tasks.push_back({6, TaskType::ReverseNumber, 12345067, 0, 6});
    for (int i = 7; i <= 20; ++i) {
        tasks.push_back(makeRandomTask(i, rng));
    }

    const int totalTasks = static_cast<int>(tasks.size());
    const int workerCount = 3;
    ThreadPool pool(workerCount);
    std::vector<std::pair<int, std::future<std::string>>> futures;
    futures.reserve(tasks.size());

    const auto start = std::chrono::high_resolution_clock::now();
    for (const auto& task : tasks) {
        futures.emplace_back(task.id, pool.enqueue(task));
    }
    pool.closeSubmission();
    pool.waitUntilDone(totalTasks);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::vector<std::pair<int, std::string>> results;
    results.reserve(totalTasks);
    for (auto& entry : futures) {
        results.emplace_back(entry.first, entry.second.get());
    }
    std::sort(results.begin(), results.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    std::cout << "Результаты задач:\n";
    for (const auto& [id, text] : results) {
        std::cout << "Задача #" << id << ": " << text << "\n";
    }
    std::cout << "\nОбработано задач: " << pool.completed() << "\n";
    std::cout << "Потоков: " << workerCount << "\n";
    std::cout << "Общее время: " << elapsedMs << " мс\n";

    return 0;
}