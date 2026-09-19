#include "util/WorkQueue.h"

#include <objbase.h>

#include "util/Log.h"

namespace cola::util {

WorkQueue::WorkQueue(bool initCom) : initCom_(initCom), thread_([this] { Run(); }) {}

WorkQueue::~WorkQueue() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        tasks_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WorkQueue::Post(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void WorkQueue::Clear() {
    std::lock_guard lock(mutex_);
    tasks_.clear();
}

void WorkQueue::Run() {
    bool comReady = false;
    if (initCom_) comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_) break;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        try {
            task();
        } catch (const std::exception& e) {
            LogError(L"Excepción en tarea de fondo: %hs", e.what());
        } catch (...) {
            LogError(L"Excepción desconocida en tarea de fondo");
        }
    }

    if (comReady) CoUninitialize();
}

}  // namespace cola::util
