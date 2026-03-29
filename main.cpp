#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <vector>
#include <windows.h>
#include <boost/thread.hpp>

struct MatrixTask {
    std::size_t rowBegin = 0;
    std::size_t rowEnd = 0;
    std::size_t colBegin = 0;
    std::size_t colEnd = 0;
};

void sumMatrixPart(
    const std::vector<std::vector<int>>& matrix,
    const MatrixTask task,
    std::int64_t& globalSum,
    std::mutex& sumMutex,
    std::atomic<int>& doneBlocks,
    std::mutex& waitMutex,
    std::condition_variable& waitCv
) {
    std::int64_t localSum = 0;
    for (std::size_t r = task.rowBegin; r < task.rowEnd; ++r) {
        for (std::size_t c = task.colBegin; c < task.colEnd; ++c) {
            localSum += matrix[r][c];
        }
    }

    {
        std::lock_guard<std::mutex> lock(sumMutex);
        globalSum += localSum;
    }

    ++doneBlocks;
    waitCv.notify_one();
}

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    const std::size_t rows = 1000;
    const std::size_t cols = 1000;
    const int threadCount = 4;
    const std::size_t blockSize = 250;

    std::vector<std::vector<int>> matrix(rows, std::vector<int>(cols, 0));
    std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> valueDist(1, 100);

    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            matrix[r][c] = valueDist(rng);
        }
    }

    std::vector<MatrixTask> tasks;
    for (std::size_t r = 0; r < rows; r += blockSize) {
        for (std::size_t c = 0; c < cols; c += blockSize) {
            tasks.push_back({
                r,
                std::min(r + blockSize, rows),
                c,
                std::min(c + blockSize, cols)
            });
        }
    }

    std::int64_t totalSum = 0;
    std::mutex sumMutex;
    std::atomic<int> doneBlocks{0};
    std::mutex waitMutex;
    std::condition_variable waitCv;

    std::atomic<std::size_t> nextTask{0};
    std::vector<boost::thread> workers;
    workers.reserve(threadCount);

    const auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < threadCount; ++i) {
        workers.emplace_back([&]() {
            while (true) {
                const std::size_t index = nextTask.fetch_add(1);
                if (index >= tasks.size()) {
                    return;
                }
                sumMatrixPart(matrix, tasks[index], totalSum, sumMutex, doneBlocks, waitMutex, waitCv);
            }
        });
    }

    {
        std::unique_lock<std::mutex> lock(waitMutex);
        waitCv.wait(lock, [&]() {
            return doneBlocks.load() == static_cast<int>(tasks.size());
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    const auto finish = std::chrono::high_resolution_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

    std::cout << "Размер матрицы: " << rows << "x" << cols << "\n";
    std::cout << "Количество потоков: " << threadCount << "\n";
    std::cout << "Размер блока: " << blockSize << "x" << blockSize << "\n";
    std::cout << "Обработано блоков: " << doneBlocks.load() << "\n";
    std::cout << "Итоговая сумма элементов: " << totalSum << "\n";
    std::cout << "Время выполнения: " << elapsedMs << " мс\n";

    return 0;
}