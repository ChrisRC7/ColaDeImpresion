#pragma once
// Escritor mínimo de archivos ZIP (sin compresión, método "stored"). Basta para
// armar paquetes Office Open XML (.xlsx) sin depender de bibliotecas externas.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cola::util {

// CRC-32 (polinomio 0xEDB88320, el de ZIP).
std::uint32_t Crc32(std::string_view data);

class ZipWriter {
public:
    // name: ruta dentro del ZIP con "/" (UTF-8). data: contenido tal cual.
    void Add(const std::string& name, std::string data);
    // Bytes del archivo ZIP completo.
    std::string Finish() const;

private:
    struct Entry {
        std::string name;
        std::string data;
    };
    std::vector<Entry> entries_;
};

}  // namespace cola::util
