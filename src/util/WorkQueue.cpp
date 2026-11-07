// Implementación del hilo de trabajo con cola FIFO (ver WorkQueue.h).

#include "util/WorkQueue.h"

#include <objbase.h>

#include "util/Log.h"

namespace cola::util {

// thread_ se inicia al final de la lista (es el último miembro), cuando el
// resto ya está construido; el hilo entra en Run() de inmediato.
WorkQueue::WorkQueue(bool initCom) : initCom_(initCom), thread_([this] { Run(); }) {}

WorkQueue::~WorkQueue() {
    {
        // Se pide parar y se tiran las pendientes bajo el mutex, así el hilo
        // no puede tomar una tarea nueva entre ambas cosas.
        std::lock_guard lock(mutex_);
        stop_ = true;
        tasks_.clear();
    }
    // Despierta al hilo si estaba esperando; si estaba ejecutando una tarea,
    // verá stop_ al volver a esperar. join() espera a que termine.
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WorkQueue::Post(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    // Se notifica fuera del candado para que el hilo despierto no tenga que
    // esperar a que se libere.
    cv_.notify_one();
}

void WorkQueue::Clear() {
    std::lock_guard lock(mutex_);
    tasks_.clear();
}

void WorkQueue::Run() {
    // COM se inicia en el propio hilo (es por hilo). MTA porque este hilo no
    // tiene bucle de mensajes, que un STA necesitaría. Solo se llama a
    // CoUninitialize si la inicialización tuvo éxito.
    bool comReady = false;
    if (initCom_) comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    for (;;) {
        std::function<void()> task;
        {
            // Espera hasta que haya algo que hacer o se pida terminar (el
            // predicado protege contra despertares espurios).
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_) break;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        // La tarea corre SIN el candado: así Post/Clear desde otros hilos (o
        // desde la propia tarea) no se bloquean ni se produce un interbloqueo.
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
