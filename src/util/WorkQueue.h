#pragma once
// Hilo de trabajo con cola FIFO de tareas. Se usa para que las llamadas al
// spooler (que pueden ser lentas con servidores remotos) y la generación de
// vistas previas nunca bloqueen el hilo de la interfaz.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace cola::util {

class WorkQueue {
public:
    // initCom: inicializa COM (MTA) en el hilo; necesario para WIC/XPS/Shell.
    explicit WorkQueue(bool initCom = false);
    ~WorkQueue();

    WorkQueue(const WorkQueue&) = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;

    void Post(std::function<void()> task);

    // Descarta las tareas pendientes (no la que se está ejecutando).
    void Clear();

private:
    void Run();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    bool stop_ = false;
    bool initCom_ = false;
    std::thread thread_;
};

}  // namespace cola::util
