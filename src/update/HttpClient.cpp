// Descarga HTTPS con WinHTTP en modo síncrono. Ver HttpClient.h.
#include "update/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include "util/Strings.h"

// Proxy automático (Windows 8.1+); por si el encabezado del compilador es viejo.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace cola::update {

// Acceso de HttpGet a los campos privados de HttpAbort (registrar y quitar la
// petición en curso) sin exponer HINTERNET en el encabezado.
struct HttpAbortAccess {
    // Registra la petición. false si ya se había abortado.
    static bool Register(HttpAbort* abort, HINTERNET request) {
        if (!abort) return true;
        std::lock_guard<std::mutex> lock(abort->mutex_);
        if (abort->aborted_) return false;
        abort->request_ = request;
        return true;
    }
    // Quita el registro. false si Abort() ya cerró la petición (no hay que
    // volver a cerrarla).
    static bool Unregister(HttpAbort* abort) {
        if (!abort) return true;
        std::lock_guard<std::mutex> lock(abort->mutex_);
        bool stillOpen = abort->request_ != nullptr;
        abort->request_ = nullptr;
        return stillOpen;
    }
};

void HttpAbort::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    aborted_ = true;
    if (request_) {
        // Cerrar el handle desde otro hilo hace fallar la operación pendiente.
        WinHttpCloseHandle(static_cast<HINTERNET>(request_));
        request_ = nullptr;
    }
}

bool HttpAbort::aborted() {
    std::lock_guard<std::mutex> lock(mutex_);
    return aborted_;
}

namespace {

// Cierra un handle de WinHTTP al salir del ámbito.
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::wstring LastErrorText(const wchar_t* step) {
    DWORD code = GetLastError();
    // Los errores de WinHTTP (12000..12199) no están en la tabla del sistema:
    // se muestra el número, que es lo que sirve para buscarlos.
    return util::Format(L"%ls (error %lu)", step, code);
}

}  // namespace

HttpResult HttpGet(const std::wstring& url, std::uint64_t maxBytes, HttpAbort* abort) {
    HttpResult result;
    if (abort && abort->aborted()) {
        result.error = L"cancelado";
        return result;
    }

    // Separa la dirección en servidor, puerto y ruta.
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    std::wstring path(url.size() + 1, L'\0');
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    parts.lpszUrlPath = path.data();
    parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        result.error = L"la dirección no es https";
        return result;
    }
    path.resize(parts.dwUrlPathLength);

    Handle session;
    const std::wstring agent = std::wstring(L"ColaDeImpresion/") + COLA_VERSION_STRING;
    session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    // Windows anteriores a 8.1 no conocen el proxy automático.
    if (!session.h)
        session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        result.error = LastErrorText(L"no se pudo iniciar WinHTTP");
        return result;
    }
    // Tiempos límite: resolver, conectar, enviar y recibir (ms).
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 30000);

    Handle connection;
    connection.h = WinHttpConnect(session.h, host, parts.nPort, 0);
    if (!connection.h) {
        result.error = LastErrorText(L"no se pudo conectar");
        return result;
    }
    HINTERNET request = WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        result.error = LastErrorText(L"no se pudo crear la petición");
        return result;
    }
    // Redirecciones solo de HTTPS a HTTPS (es el valor predeterminado; se fija
    // para que no dependa de él).
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    if (!HttpAbortAccess::Register(abort, request)) {
        WinHttpCloseHandle(request);
        result.error = L"cancelado";
        return result;
    }

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (!ok) result.error = LastErrorText(L"no se pudo conectar con el servidor");

    if (ok) {
        DWORD status = 0, statusSize = sizeof(status);
        ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        result.status = status;
        if (!ok) result.error = LastErrorText(L"respuesta sin código de estado");
        else if (status != 200) {
            ok = false;
            result.error = util::Format(L"el servidor respondió %lu", status);
        }
    }

    // Cuerpo por tramos, con el tope de tamaño.
    std::string buffer(64 * 1024, '\0');
    while (ok) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            ok = false;
            result.error = LastErrorText(L"se cortó la descarga");
            break;
        }
        if (read == 0) break;  // fin de la respuesta
        if (result.body.size() + read > maxBytes) {
            ok = false;
            result.error = L"la respuesta es más grande de lo esperado";
            break;
        }
        result.body.append(buffer.data(), read);
    }

    // Si Abort() ya cerró la petición, no se cierra otra vez.
    if (HttpAbortAccess::Unregister(abort)) WinHttpCloseHandle(request);
    else if (!ok) result.error = L"cancelado";
    if (!ok) result.body.clear();
    result.ok = ok;
    return result;
}

}  // namespace cola::update
