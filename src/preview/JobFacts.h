#pragma once
// Análisis en segundo plano de los datos de TODOS los trabajos de la cola (no
// solo del seleccionado), para que la lista muestre las páginas que se van a
// imprimir en total (páginas × copias). Las copias reales de un trabajo
// enviado desde otra PC solo están en sus datos (EJL de Epson, PJL, DSC,
// DEVMODE del spool EMF), no en JOB_INFO_2.
//
// Cada trabajo se lee una sola vez con la API del spooler (se guardan como
// máximo kMaxBytes: el encabezado trae las opciones; de un ESC/Page más largo
// el resto se recorre sin guardarlo para contar sus páginas) y el resultado queda en caché
// hasta que el trabajo sale de la cola. Los trabajos que todavía se reciben se
// analizan al terminar. Al liberarlo (pasa a imprimirse) sus datos siguen
// valiendo aunque el spooler informe otro tamaño, para que la fila y el
// historial muestren lo mismo que mientras estaba retenido. No se renderiza
// nada ni se guarda contenido: solo los ContentFacts.
//
// Decisiones: D31 (Páginas = páginas × copias con los datos de todos los
// trabajos), D32 (lecturas ReadPrinter serializadas), D42 (los datos valen
// mientras el trabajo esté en la cola), D48 (lecturas incompletas por varias
// copias de la app) y D53 (páginas ESC/Page contadas en todos los datos).
//
// Hilos: Find/Request/Store/Clear se llaman desde la UI (o desde el hilo de
// la vista previa en el caso de Store) y solo toman mutex_ un instante; las
// lecturas y el análisis corren en el hilo propio (worker_, con COM para el
// XPS Object Model), un trabajo a la vez.

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

// Caché de ContentFacts por trabajo más el hilo que los obtiene. Lo posee
// MainWindow; RowBuilder lo consulta (Find) para armar las filas.
class JobFactsService {
public:
    // Se invoca desde el hilo de fondo cuando hay resultados nuevos: reenviar a la UI.
    using ReadyCallback = std::function<void()>;

    // onReady: aviso de "hay datos nuevos" (la UI vuelve a armar las filas).
    explicit JobFactsService(ReadyCallback onReady);

    // Datos analizados del trabajo, o nullptr si todavía no se analizó. Si su
    // tamaño cambió después de recibirse (al imprimirse), los últimos analizados.
    std::shared_ptr<const core::ContentFacts> Find(const core::JobInfo& job);

    // Encola el análisis de los trabajos que falten y olvida los que ya no están.
    // jobs: la lista completa actual (cada instantánea del monitor). server y
    // overrideDir: para la búsqueda del spool (aunque aquí solo se usa la API).
    void Request(const std::vector<core::JobInfo>& jobs, const std::wstring& server, const std::wstring& overrideDir);

    // Guarda un análisis más completo (p. ej. el de la vista previa, que lee todos los datos).
    // Ignora análisis vacíos (facts.analyzed == false).
    void Store(const core::JobInfo& job, const core::ContentFacts& facts);

    // Olvida todo y descarta lo pendiente (p. ej. al cambiar de servidor).
    void Clear();

    // Clave: impresora, id, tamaño y si todavía se recibe (cambia si el trabajo
    // cambia). No incluye el estado: pausado o imprimiéndose es el mismo trabajo.
    static std::wstring Key(const core::JobInfo& job);

    // Bytes que se guardan por trabajo para analizar (16 MB, D31): bastan
    // para los encabezados y para casi todos los trabajos completos.
    static constexpr std::uint64_t kMaxBytes = 16ull * 1024 * 1024;
    // Lectura incompleta (otra copia de la app leía el mismo trabajo): se vuelve
    // a intentar en otra ronda, como mucho kIncompleteRounds veces y no antes de
    // kIncompleteRetryMs. Siempre con el trabajo en pausa (CanReadJobData).
    static constexpr int kIncompleteRounds = 3;
    static constexpr std::uint64_t kIncompleteRetryMs = 15000;

private:
    // Impresora e id: identifica el trabajo mientras está en la cola.
    static std::wstring IdKey(const core::JobInfo& job);
    // Actualiza latest_ con un análisis con datos (uno completo no se
    // reemplaza por uno parcial). Requiere mutex_ tomado.
    void Remember(const core::JobInfo& job, const std::shared_ptr<const core::ContentFacts>& facts);

    ReadyCallback onReady_;  // aviso a la UI (se llama sin el candado tomado)
    std::mutex mutex_;       // protege cache_, latest_, pending_ y retries_
    // por Key: resultado del análisis (también los vacíos, para no reintentar).
    std::map<std::wstring, std::shared_ptr<const core::ContentFacts>> cache_;
    std::map<std::wstring, std::shared_ptr<const core::ContentFacts>> latest_;  // por IdKey: último análisis con datos
    std::set<std::wstring> pending_;  // Keys encoladas o en análisis
    // Reintentos de una lectura incompleta (D48).
    struct Retry {
        int rounds = 0;  // intentos incompletos hechos
        std::uint64_t notBefore = 0;  // GetTickCount64()
    };
    std::map<std::wstring, Retry> retries_;  // por Key: lecturas incompletas
    util::WorkQueue worker_{true};  // COM para el XPS Object Model; último miembro: se destruye primero
};

}  // namespace cola::preview
