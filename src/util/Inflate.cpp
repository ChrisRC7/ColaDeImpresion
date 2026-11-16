// Descompresor DEFLATE (RFC 1951). Ver Inflate.h.
//
// Un flujo DEFLATE es una serie de bloques; cada uno empieza con 3 bits:
//   BFINAL (1 = último bloque) y BTYPE (2 bits):
//     0 = sin comprimir (LEN, ~LEN y los bytes tal cual),
//     1 = Huffman con tablas fijas (definidas por la norma),
//     2 = Huffman con tablas dinámicas (vienen al principio del bloque).
// En los bloques comprimidos cada símbolo es un byte literal (0..255), el fin
// de bloque (256) o un largo (257..285) seguido de una distancia: "copiar
// `largo` bytes desde `distancia` bytes atrás en la salida".
// Los bits se leen empezando por el menos significativo de cada byte.
#include "util/Inflate.h"

#include <array>

namespace cola::util {

namespace {

// Lector de bits sobre el búfer de entrada. `fail` queda en true si se pide
// más de lo que hay (datos cortados); a partir de ahí devuelve ceros.
struct BitReader {
    const std::uint8_t* data;
    size_t size;
    size_t pos = 0;         // siguiente byte sin leer
    std::uint32_t buffer = 0;  // bits ya leídos y no consumidos (el próximo es el bit 0)
    int count = 0;          // cuántos bits válidos hay en buffer (siempre < 8 al salir de Bits)
    bool fail = false;

    // Devuelve los siguientes `need` bits (0..16) como número.
    std::uint32_t Bits(int need) {
        while (count < need) {
            if (pos >= size) {
                fail = true;
                return 0;
            }
            buffer |= static_cast<std::uint32_t>(data[pos++]) << count;
            count += 8;
        }
        std::uint32_t value = buffer & ((1u << need) - 1);
        buffer >>= need;
        count -= need;
        return value;
    }
};

constexpr int kMaxBits = 15;       // largo máximo de un código de Huffman
constexpr int kMaxLitCodes = 288;  // literales/largos (0..287)
constexpr int kMaxDistCodes = 30;  // distancias (0..29)

// Tabla de Huffman canónica: cuántos códigos hay de cada largo y los símbolos
// ordenados por (largo, valor). Con eso alcanza para decodificar bit a bit.
struct Huffman {
    std::array<std::uint16_t, kMaxBits + 1> count{};
    std::array<std::uint16_t, kMaxLitCodes> symbol{};

    // Arma la tabla con el largo del código de cada símbolo (0 = no se usa).
    // false si los largos no forman un código válido (más códigos de los que
    // caben: "sobre-suscrito"). Un código incompleto se acepta: la norma lo
    // permite (p. ej. un bloque con una sola distancia) y un código que no
    // exista se detecta al decodificar.
    bool Build(const std::uint8_t* lengths, int n) {
        count.fill(0);
        for (int i = 0; i < n; ++i) ++count[lengths[i]];
        count[0] = 0;
        int left = 1;  // códigos libres del largo actual
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;
        }
        // Posición del primer símbolo de cada largo dentro de `symbol`.
        std::array<std::uint16_t, kMaxBits + 2> offset{};
        for (int len = 1; len <= kMaxBits; ++len) offset[len + 1] = static_cast<std::uint16_t>(offset[len] + count[len]);
        for (int i = 0; i < n; ++i)
            if (lengths[i]) symbol[offset[lengths[i]]++] = static_cast<std::uint16_t>(i);
        return true;
    }

    // Lee un símbolo. -1 si los datos se cortan o el código no existe.
    // Recorre los largos de menor a mayor: `first` es el primer código de ese
    // largo e `index` la posición de su símbolo; si el código leído cae en el
    // rango de ese largo, ya está.
    int Decode(BitReader& in) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= static_cast<int>(in.Bits(1));
            if (in.fail) return -1;
            int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

// Largo base y bits extra de los símbolos 257..285, y lo mismo para las 30
// distancias (tablas de la RFC 1951, sección 3.2.5).
constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                         193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Decodifica los símbolos de un bloque comprimido hasta el fin de bloque (256).
bool InflateCodes(BitReader& in, const Huffman& lit, const Huffman& dist, size_t limit, std::string& out) {
    for (;;) {
        int sym = lit.Decode(in);
        if (sym < 0) return false;
        if (sym < 256) {
            if (out.size() >= limit) return false;
            out.push_back(static_cast<char>(sym));
            continue;
        }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;  // 286 y 287 no son válidos
        size_t length = kLengthBase[sym] + in.Bits(kLengthExtra[sym]);
        int dsym = dist.Decode(in);
        if (dsym < 0 || dsym >= 30) return false;
        size_t distance = kDistBase[dsym] + in.Bits(kDistExtra[dsym]);
        if (in.fail || distance > out.size() || length > limit - out.size()) return false;
        // Byte a byte: el origen y el destino pueden solaparse (distancia
        // menor que el largo repite lo recién escrito).
        size_t from = out.size() - distance;
        for (size_t i = 0; i < length; ++i) out.push_back(out[from + i]);
    }
}

// Bloque BTYPE 0: descarta los bits que faltan del byte actual, lee LEN y su
// complemento y copia LEN bytes.
bool InflateStored(BitReader& in, size_t limit, std::string& out) {
    in.buffer = 0;
    in.count = 0;
    if (in.size - in.pos < 4) return false;
    unsigned len = in.data[in.pos] | (in.data[in.pos + 1] << 8);
    unsigned nlen = in.data[in.pos + 2] | (in.data[in.pos + 3] << 8);
    in.pos += 4;
    if ((len ^ 0xFFFFu) != nlen) return false;
    if (in.size - in.pos < len || len > limit - out.size()) return false;
    out.append(reinterpret_cast<const char*>(in.data + in.pos), len);
    in.pos += len;
    return true;
}

// Bloque BTYPE 1: tablas fijas de la norma.
bool InflateFixed(BitReader& in, size_t limit, std::string& out) {
    std::uint8_t lengths[kMaxLitCodes];
    int i = 0;
    for (; i < 144; ++i) lengths[i] = 8;
    for (; i < 256; ++i) lengths[i] = 9;
    for (; i < 280; ++i) lengths[i] = 7;
    for (; i < kMaxLitCodes; ++i) lengths[i] = 8;
    Huffman lit, dist;
    lit.Build(lengths, kMaxLitCodes);
    for (i = 0; i < kMaxDistCodes; ++i) lengths[i] = 5;
    dist.Build(lengths, kMaxDistCodes);
    return InflateCodes(in, lit, dist, limit, out);
}

// Bloque BTYPE 2: primero vienen los largos de los códigos, a su vez
// comprimidos con un tercer código de Huffman ("code length code").
bool InflateDynamic(BitReader& in, size_t limit, std::string& out) {
    int nlen = static_cast<int>(in.Bits(5)) + 257;  // literales/largos
    int ndist = static_cast<int>(in.Bits(5)) + 1;   // distancias
    int ncode = static_cast<int>(in.Bits(4)) + 4;   // largos del código de largos
    if (in.fail || nlen > 286 || ndist > kMaxDistCodes) return false;

    // Los largos del código de largos vienen en este orden fijo.
    static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::uint8_t lengths[kMaxLitCodes + kMaxDistCodes] = {};
    for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<std::uint8_t>(in.Bits(3));
    if (in.fail) return false;
    Huffman codeLengths;
    if (!codeLengths.Build(lengths, 19)) return false;

    // Largos de literales/largos y de distancias, seguidos, con repeticiones:
    // 16 = repetir el anterior 3..6 veces, 17 = 3..10 ceros, 18 = 11..138 ceros.
    for (int i = 0; i < 19; ++i) lengths[i] = 0;
    int index = 0;
    while (index < nlen + ndist) {
        int sym = codeLengths.Decode(in);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[index++] = static_cast<std::uint8_t>(sym);
            continue;
        }
        std::uint8_t value = 0;
        int repeat = 0;
        if (sym == 16) {
            if (index == 0) return false;  // no hay anterior que repetir
            value = lengths[index - 1];
            repeat = 3 + static_cast<int>(in.Bits(2));
        } else if (sym == 17) {
            repeat = 3 + static_cast<int>(in.Bits(3));
        } else {
            repeat = 11 + static_cast<int>(in.Bits(7));
        }
        if (in.fail || index + repeat > nlen + ndist) return false;
        while (repeat--) lengths[index++] = value;
    }
    if (lengths[256] == 0) return false;  // sin código de fin de bloque

    Huffman lit, dist;
    if (!lit.Build(lengths, nlen)) return false;
    if (!dist.Build(lengths + nlen, ndist)) return false;
    return InflateCodes(in, lit, dist, limit, out);
}

}  // namespace

bool Inflate(const std::uint8_t* data, size_t size, size_t expectedSize, std::string& out) {
    out.clear();
    out.reserve(expectedSize);
    BitReader in{data, size};
    for (;;) {
        bool last = in.Bits(1) != 0;
        std::uint32_t type = in.Bits(2);
        if (in.fail) return false;
        bool ok = false;
        if (type == 0) ok = InflateStored(in, expectedSize, out);
        else if (type == 1) ok = InflateFixed(in, expectedSize, out);
        else if (type == 2) ok = InflateDynamic(in, expectedSize, out);
        if (!ok) return false;
        if (last) break;
    }
    return out.size() == expectedSize;
}

}  // namespace cola::util
