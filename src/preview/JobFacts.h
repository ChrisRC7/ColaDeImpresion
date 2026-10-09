#pragma once
// Análisis en segundo plano de los datos de TODOS los trabajos de la cola (no
// solo del seleccionado), para que la lista muestre las páginas que se van a
// imprimir en total (páginas × copias). Las copias reales de un trabajo
// enviado desde otra PC solo están en sus datos (EJL de Epson, PJL, DSC,
// DEVMODE del spool EMF), no en JOB_INFO_2.
//
// Cada trabajo se lee una sola vez con la API del spooler (como máximo
// kMaxBytes: el encabezado trae las opciones) y el resultado queda en caché
// hasta que el trabajo sale de la cola. Los trabajos que todavía se reciben se
// analizan al terminar. Al liberarlo (pasa a imprimirse) sus datos siguen
// valiendo aunque el spooler informe otro tamaño, para que la fila y el
// historial muestren lo mismo que mientras estaba retenido. No se renderiza
// nada ni se guarda contenido: solo los ContentFacts.

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/PageAccounting.h"
#include "core/PrintTypes.h"
#include "util/WorkQueue.h"

namespace cola::preview {

class JobFactsService {
public:
    // Se invoca desde el hilo de fondo cuando hay resultados nuevos: reenviar a la UI.
    using ReadyCallback = std::function<void()>;

    explicit JobFactsService(ReadyCallback onReady);

    // Datos analizados del trabajo, o nullptr si todavía no se analizó. Si su
    // tamaño cambió después de recibirse (al imprimirse), los últimos analizados.
    std::shared_ptr<const core::ContentFacts> Find(const core::JobInfo& job);

    // Encola el análisis de los trabajos que falten y olvida los que ya no están.
    void Request(const std::vector<core::JobInfo>& jobs, const std::wstring& server, const std::wstring& overrideDir);

    // Guarda un análisis más completo (p. ej. el de la vista previa, que lee todos los datos).
    void Store(const core::JobInfo& job, const core::ContentFacts& facts);

    void Clear();

    // Clave: impresora, id, tamaño y si todavía se recibe (cambia si el trabajo
    // cambia). No incluye el estado: pausado o imprimiéndose es el mismo trabajo.
    static std::wstring Key(const core::JobInfo& job);

    static constexpr std::uint64_t kMaxBytes = 16ull * 1024 * 1024;

private:
    // Impresora e id: identifica el trabajo mientras está en la cola.
    static std::wstring IdKey(const core::JobInfo& job);
    void Remember(const core::JobInfo& job, const std::shared_ptr<const core::ContentFacts>& facts);

    ReadyCallback onReady_;
    std::mutex mutex_;
    std::map<std::wstring, std::shared_ptr<const core::ContentFacts>> cache_;
    std::map<std::wstring, std::shared_ptr<const core::ContentFacts>> latest_;  // por IdKey: último análisis con datos
    std::set<std::wstring> pending_;
    util::WorkQueue worker_{true};  // COM para el XPS Object Model; último miembro: se destruye primero
};

}  // namespace cola::preview
