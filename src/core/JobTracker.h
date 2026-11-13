#pragma once
// Compara instantáneas consecutivas de la cola y genera eventos:
// trabajo nuevo, trabajo que entra en error, trabajo que sale de la cola
// (con una estimación de si se imprimió o se canceló). Es lógica pura y se
// prueba en tests/.
//
// Existe porque el spooler no guarda historial (D13): MainWindow::OnSnapshot
// llama a Update con cada instantánea y convierte los eventos en registros de
// historial.json y en notificaciones. Decisiones relacionadas: D42 (se
// conserva el mayor tamaño/páginas visto), D47 (marcas de lo que hizo el
// operador y exclusión de la copia local de CSR) y D52 (no se pierden trabajos
// de una impresora que deja de responder).
//
// Hilos: no es thread-safe; lo usa solo el hilo de la UI.

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/History.h"
#include "core/PrintTypes.h"

namespace cola::core {

// Un cambio detectado entre dos instantáneas.
struct JobEvent {
    // Added: apareció un trabajo nuevo. EnteredError: pasó a error (o llegó ya
    // en error/fuera de línea). Finished: salió de la cola (va al historial).
    enum class Kind { Added, EnteredError, Finished };
    Kind kind = Kind::Added;
    // Último estado conocido del trabajo, con el mayor tamaño y la mayor
    // cantidad de páginas que tuvo (al imprimirse el spooler puede bajarlos).
    JobInfo job;
    config::JobOutcome outcome = config::JobOutcome::Printed;  // solo para Finished
};

// Seguimiento de los trabajos entre instantáneas de un mismo servidor.
class JobTracker {
public:
    // Devuelve los eventos entre la instantánea anterior y la nueva.
    // La primera llamada (o tras Reset) no genera eventos "Added".
    // Si cambia el servidor de la instantánea se reinicia solo (Reset). Una
    // instantánea con error global no genera eventos ni reemplaza la memoria
    // (una lista vacía por un fallo no significa que todo terminó).
    std::vector<JobEvent> Update(const QueueSnapshot& snapshot);

    // La app marca los trabajos que ella misma canceló para clasificarlos bien.
    // (jobKey = JobInfo::Key; la marca se consume cuando el trabajo sale.)
    void MarkCancelledByUser(const std::wstring& jobKey) { cancelledByUs_.insert(jobKey); }
    // Se retira la marca cuando la cancelación no se hizo (acceso denegado,
    // no se pudo vaciar la cola, reenvío fallido o rechazado): si el trabajo
    // se imprime después, el historial debe decir Impreso y no Cancelado.
    void ForgetCancelledByUser(const std::wstring& jobKey) { cancelledByUs_.erase(jobKey); }
    // ...y los que liberó (reanudar, reiniciar, imprimir y volver a pausar):
    // un trabajo corto se imprime y sale de la cola antes del siguiente
    // refresco, así que lo último que se vio de él es "en pausa" con la
    // impresora en pausa, y sin esta marca quedaba como "retirado".
    void MarkReleasedByUser(const std::wstring& jobKey) { releasedByUs_.insert(jobKey); }
    // Se olvida la liberación cuando el operador lo vuelve a pausar (D47): si
    // luego sale de la cola en pausa, no hay motivo para darlo por impreso.
    void ForgetReleasedByUser(const std::wstring& jobKey) { releasedByUs_.erase(jobKey); }

    // Olvida todo: trabajos vistos, problemas de impresoras y marcas.
    void Reset();

private:
    // Trabajos de la última instantánea fiable (clave JobInfo::Key), con el
    // mayor tamaño/páginas visto. Incluye los de impresoras sin respuesta (D52).
    std::unordered_map<std::wstring, JobInfo> last_;
    // Problemas de cada impresora en la última instantánea fiable (o los de
    // antes del corte si ahora no responde): entrada de GuessOutcome.
    std::unordered_map<std::wstring, std::uint32_t> printerProblems_;
    std::set<std::wstring> cancelledByUs_;  // claves que canceló esta app
    std::set<std::wstring> releasedByUs_;   // claves que liberó esta app (D47)
    bool initialized_ = false;  // ya hubo una instantánea fiable (base de comparación)
    std::wstring server_;       // servidor de las instantáneas que se comparan
};

// Heurística de resultado para un trabajo que desapareció de la cola.
//  last:            lo último que se vio del trabajo.
//  printerProblems: problemas de su impresora en esa última instantánea.
//  cancelledByUs:   la app lo canceló (-> Cancelado).
//  releasedByUs:    la app lo liberó (-> Impreso si no hay indicios contrarios).
// Orden de las reglas: cancelado > impreso (se vio imprimiéndose o con páginas
// impresas) > falló (último estado error) > liberado por el operador > primero
// de una cola operativa (impreso entre dos refrescos) > retirado.
config::JobOutcome GuessOutcome(const JobInfo& last, std::uint32_t printerProblems, bool cancelledByUs,
                                bool releasedByUs = false);

}  // namespace cola::core
