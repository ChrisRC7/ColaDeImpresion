#pragma once
// Descarga HTTPS mínima con WinHTTP (viene con Windows: sin dependencias).
//
// Solo la usa la actualización automática (D58) para bajar el manifiesto y el
// paquete desde GitHub. Reglas:
//   - solo direcciones https:// (nunca http); las redirecciones se siguen
//     únicamente de HTTPS a HTTPS (GitHub redirige las descargas a su CDN);
//   - el certificado del servidor lo valida Windows (nada se desactiva);
//   - se respeta el proxy configurado en Windows;
//   - la respuesta se guarda en memoria con un tope de tamaño;
//   - no se envían credenciales, cookies ni datos del equipo o del usuario.
// Las llamadas bloquean: usar desde un hilo de fondo.

#include <cstdint>
#include <mutex>
#include <string>

namespace cola::update {

// Permite cortar desde otro hilo una descarga en curso (al cerrar la app).
// Abort() cierra la petición activa, con lo que la llamada bloqueada de
// WinHTTP vuelve con error; las siguientes HttpGet con este objeto fallan
// enseguida.
class HttpAbort {
public:
    void Abort();
    bool aborted();

private:
    friend struct HttpAbortAccess;
    std::mutex mutex_;
    void* request_ = nullptr;  // HINTERNET de la petición en curso, o nullptr
    bool aborted_ = false;
};

// Resultado de HttpGet.
struct HttpResult {
    bool ok = false;           // respuesta 200 recibida completa
    unsigned long status = 0;  // código HTTP (0 si no se llegó a recibir)
    std::string body;          // contenido (solo si ok)
    std::wstring error;        // motivo legible si !ok
};

// GET de `url` (https://). Falla si la respuesta no es 200, si supera
// `maxBytes` o si se llamó a abort->Abort(). `abort` puede ser nullptr.
HttpResult HttpGet(const std::wstring& url, std::uint64_t maxBytes, HttpAbort* abort = nullptr);

}  // namespace cola::update
