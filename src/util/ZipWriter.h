#pragma once
// Escritor mínimo de archivos ZIP (sin compresión, método "stored"). Basta para
// armar paquetes Office Open XML (.xlsx) sin depender de bibliotecas externas.
// (Decisión D43: un .xlsx mínimo son unos pocos XML dentro de un ZIP; Excel
// acepta entradas sin comprimir.)
//
// Limitaciones (aceptables para el historial): sin ZIP64, así que cada
// entrada y el archivo completo deben medir menos de 4 GB y haber menos de
// 65 535 entradas; todo se arma en memoria.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cola::util {

// CRC-32 (polinomio 0xEDB88320, el de ZIP).
// Suma de verificación que el formato ZIP exige para cada entrada.
std::uint32_t Crc32(std::string_view data);

// Acumula entradas con Add() y produce el ZIP con Finish(). No escribe a
// disco: el llamador guarda los bytes devueltos donde quiera.
class ZipWriter {
public:
    // name: ruta dentro del ZIP con "/" (UTF-8). data: contenido tal cual.
    // Las entradas se escriben en el orden en que se agregan (en .xlsx
    // conviene que "[Content_Types].xml" vaya primero).
    void Add(const std::string& name, std::string data);
    // Bytes del archivo ZIP completo.
    // Se puede llamar varias veces; no modifica las entradas.
    std::string Finish() const;

private:
    // Una entrada pendiente: ruta interna y contenido sin comprimir.
    struct Entry {
        std::string name;
        std::string data;
    };
    std::vector<Entry> entries_;
};

}  // namespace cola::util
