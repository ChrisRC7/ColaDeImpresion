#pragma once
// Actualización automática (D58): servicio en segundo plano.
//
// Une las piezas del módulo: pide el manifiesto de la última publicación
// (UpdateInfo + HttpClient), y si trae una versión más nueva que la que corre,
// descarga el paquete, comprueba su tamaño y su SHA-256 y lo instala en la
// carpeta del .exe (UpdatePackage). La app sigue abierta con la versión vieja;
// la nueva se usa al reiniciarla (lo decide el operador: la app retiene y
// pausa trabajos, así que nunca se reinicia sola).
//
// Hilos: Check() se llama desde la UI y no bloquea; todo el trabajo corre en
// el hilo de worker_, de a una comprobación por vez. El resultado llega por el
// callback DESDE ESE HILO: quien lo recibe debe reenviarlo a la UI con
// PostMessage. El destructor corta la descarga en curso (HttpAbort) y espera a
// que termine la tarea; una instalación ya empezada no se interrumpe.

#include <functional>
#include <string>

#include "update/HttpClient.h"
#include "update/UpdateInfo.h"
#include "util/WorkQueue.h"

namespace cola::update {

// Qué pasó en una comprobación (o en qué paso va).
enum class UpdateState {
    Checking,     // consultando la última versión publicada
    UpToDate,     // no hay nada más nuevo
    Available,    // hay una versión nueva y no se pidió instalarla
    Downloading,  // bajando el paquete
    Installed,    // la versión nueva ya está en disco: falta reiniciar la app
    NotWritable,  // hay versión nueva pero no se puede escribir en la carpeta de la app
    Failed,       // no se pudo comprobar, descargar o instalar (ver `error`)
};

// Aviso que recibe la UI.
struct UpdateStatus {
    UpdateState state = UpdateState::Failed;
    Version version;     // versión nueva (Available, Downloading, Installed, NotWritable)
    std::wstring notes;  // novedades del manifiesto, si las trae
    std::wstring error;  // motivo (Failed)
    bool manual = false; // la pidió el operador desde el menú (se le responde siempre)
};

class UpdateService {
public:
    using Callback = std::function<void(UpdateStatus)>;

    // onStatus se invoca desde el hilo de fondo. Al crearse, el servicio
    // limpia los restos de la actualización anterior (CleanupOldFiles).
    explicit UpdateService(Callback onStatus);
    ~UpdateService();

    // Encola una comprobación. manual: la pidió el operador. install: si hay
    // versión nueva, descargarla e instalarla (si no, solo se avisa Available).
    void Check(bool manual, bool install);

private:
    // Tarea de worker_.
    void Run(bool manual, bool install);

    Callback onStatus_;
    HttpAbort abort_;         // corta la descarga al destruir el servicio
    util::WorkQueue worker_;  // último miembro: se destruye (y une) primero
};

// Versión del ColaDeImpresion.exe que hay en el disco (su recurso de versión).
// Puede ser más nueva que la que corre si ya se instaló una actualización
// (esta sesión u otra) y falta reiniciar. false si no se puede leer.
bool InstalledVersion(Version& out);

}  // namespace cola::update
