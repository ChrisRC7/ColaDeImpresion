#pragma once
// Hilo de trabajo con cola FIFO de tareas. Se usa para que las llamadas al
// spooler (que pueden ser lentas con servidores remotos) y la generación de
// vistas previas nunca bloqueen el hilo de la interfaz.
//
// Cada WorkQueue tiene UN hilo propio: las tareas se ejecutan de a una y en el
// orden en que se publicaron (útil para serializar, p. ej., las acciones sobre
// trabajos). Las tareas que necesiten avisar a la UI deben hacerlo con
// PostMessage (ver CONTEXTO.md §3.2), nunca tocando ventanas directamente.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace cola::util {

class WorkQueue {
public:
    // initCom: inicializa COM (MTA) en el hilo; necesario para WIC/XPS/Shell.
    // El hilo arranca en el constructor y queda esperando tareas.
    explicit WorkQueue(bool initCom = false);
    // Descarta las tareas pendientes, espera a que termine la que se esté
    // ejecutando y une el hilo. Una tarea larga (p. ej. una lectura del
    // spooler) puede demorar el cierre: las tareas deberían ser cortas o
    // comprobar algún indicador de cancelación propio.
    ~WorkQueue();

    // No copiable: es dueño de un hilo.
    WorkQueue(const WorkQueue&) = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;

    // Encola una tarea al final de la cola y despierta al hilo. Se puede
    // llamar desde cualquier hilo (incluso desde una tarea de esta cola).
    void Post(std::function<void()> task);

    // Descarta las tareas pendientes (no la que se está ejecutando).
    // Se usa cuando las pendientes ya no sirven (p. ej. se cambió de servidor).
    void Clear();

private:
    // Bucle del hilo: espera tareas, las ejecuta y atrapa sus excepciones
    // (registradas en el log) para que una tarea fallida no mate el hilo.
    void Run();

    // Protegen tasks_ y stop_.
    std::mutex mutex_;
    std::condition_variable cv_;
    // Tareas pendientes en orden FIFO.
    std::deque<std::function<void()>> tasks_;
    // true = el destructor pidió terminar.
    bool stop_ = false;
    // Si el hilo debe llamar a CoInitializeEx al empezar.
    bool initCom_ = false;
    // Debe declararse el ÚLTIMO: los miembros se inician en orden de
    // declaración y el hilo empieza a usar mutex_, cv_, tasks_, etc. apenas
    // se crea, así que tienen que estar construidos antes.
    std::thread thread_;
};

}  // namespace cola::util
