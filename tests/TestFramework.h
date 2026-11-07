#pragma once
// Mini framework de pruebas sin dependencias.
//
//   TEST(Nombre) { CHECK(expr); CHECK_EQ(a, b); }
//
// Las pruebas se registran solas; test_main.cpp las ejecuta todas.
//
// Por qué uno propio: la regla del proyecto es no agregar dependencias externas
// (D1/D10 de docs/CONTEXTO.md); GoogleTest o Catch2 obligarían a descargarlos y
// compilarlos con WinLibs. Lo que hace falta es poco: registrar funciones,
// contar fallos y mostrar el archivo:línea del CHECK que falló.
//
// Cómo funciona:
//   1. TEST(Nombre) declara una función estática test_Nombre y un objeto global
//      registrar_Nombre (testing::Registrar) cuyo constructor agrega
//      {"Nombre", test_Nombre} a testing::Registry() antes de que corra wmain.
//      Luego la macro termina con la cabecera de la función, así que el bloque
//      { ... } que escribe la prueba es su cuerpo.
//   2. test_main.cpp recorre Registry() en orden de registro (dentro de cada .cpp
//      es el orden del archivo; entre archivos depende del enlazador), ejecuta
//      cada cuerpo y compara FailureCount() antes y después para decidir si esa
//      prueba falló.
//   3. Un CHECK que falla NO detiene la prueba: solo suma un fallo y escribe
//      "FALLO archivo:línea: expresión" en stderr. Así se ven todos los fallos de
//      una prueba de una vez. (Por eso conviene CHECK(p != nullptr) y luego
//      "if (p)" antes de usar p.)
//   4. El código de salida del ejecutable es 0 si todas pasaron y 1 si no, que
//      es lo que miran ctest y scripts/build.ps1.
//
// Los nombres de las pruebas deben ser identificadores válidos y únicos en
// todo el ejecutable (generan test_<Nombre> y registrar_<Nombre>, estáticos por
// archivo, pero un nombre repetido se vería dos veces en la salida).

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

// Una prueba registrada: el nombre (texto literal de TEST(...)) y su cuerpo.
struct TestCase {
    const char* name;
    std::function<void()> body;
};

// Lista global de pruebas. Es un "static" local de función (no una variable
// global) para evitar el problema del orden de inicialización entre unidades de
// traducción: los Registrar de cada test_*.cpp se construyen antes de main y la
// lista debe existir ya en ese momento, sin importar qué .cpp se inicializa
// primero.
inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

// Total de CHECK fallidos en toda la ejecución (no por prueba): test_main.cpp
// lo lee antes y después de cada prueba para saber si esa prueba falló.
inline int& FailureCount() {
    static int failures = 0;
    return failures;
}

// Objeto auxiliar que crea TEST(...): su único trabajo es registrar la prueba
// en el constructor, que corre durante la inicialización estática.
struct Registrar {
    Registrar(const char* name, std::function<void()> body) { Registry().push_back({name, std::move(body)}); }
};

// Cuenta un fallo y lo informa con archivo y línea (los da __FILE__/__LINE__ de
// la macro que falló). No lanza excepciones: la prueba sigue ejecutándose.
inline void ReportFailure(const char* file, int line, const std::string& message) {
    ++FailureCount();
    std::cerr << "  FALLO " << file << ":" << line << ": " << message << "\n";
}

// Convierte un valor a texto para el mensaje de CHECK_EQ. Cualquier tipo con
// operator<< sirve (números, std::string, const char*...). std::wstring no se
// puede escribir en std::cout/ostringstream de char, así que se pasa carácter
// por carácter: lo ASCII tal cual y el resto como '?' (acentos, ñ, comillas
// tipográficas...). Es solo para diagnóstico: la comparación ya se hizo con los
// valores originales. Un tipo sin operator<< (por ejemplo un enum class) no
// compila con CHECK_EQ: para esos se usa CHECK(a == b).
template <typename T>
std::string ToText(const T& value) {
    std::ostringstream out;
    if constexpr (std::is_same_v<T, std::wstring>) {
        for (wchar_t c : value) out << (c < 128 ? static_cast<char>(c) : '?');
    } else {
        out << value;
    }
    return out.str();
}

}  // namespace testing

// TEST_CONCAT pega dos fragmentos en un identificador (test_ + Nombre). Hacen
// falta dos niveles para que, si un argumento fuera a su vez una macro, se
// expanda antes de pegar (con ## directo se pegaría el nombre de la macro).
#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)

// TEST(Nombre) { ... } define y registra una prueba. Se expande a tres cosas:
//   - la declaración adelantada de "static void test_Nombre();" (el Registrar
//     necesita su dirección antes de que aparezca el cuerpo);
//   - el objeto estático registrar_Nombre, que la agrega a Registry() con el
//     nombre como texto (#name);
//   - la cabecera "static void test_Nombre()", a la que se pega el bloque { }
//     escrito después de la macro.
// Todo es "static": cada test_*.cpp tiene sus propias funciones sin chocar en
// el enlazado con otros archivos.
#define TEST(name)                                                                         \
    static void TEST_CONCAT(test_, name)();                                                \
    static testing::Registrar TEST_CONCAT(registrar_, name)(#name, TEST_CONCAT(test_, name)); \
    static void TEST_CONCAT(test_, name)()

// CHECK(expr): si expr es falsa, registra un fallo con el texto literal de la
// expresión (#expr). La prueba continúa. El "do { } while (0)" hace que la macro
// se comporte como una sola sentencia (sirve tras un if sin llaves y exige el
// punto y coma final).
#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) testing::ReportFailure(__FILE__, __LINE__, #expr); \
    } while (0)

// CHECK_EQ(a, b): compara con == y, si difieren, muestra ambas expresiones y sus
// valores ("a == b  (valor_a vs valor_b)"), que es lo que permite diagnosticar
// sin depurador. Por convención el primer argumento es lo calculado y el
// segundo lo esperado.
//
// Copia sus argumentos A PROPÓSITO (const auto va_ = (a);) en vez de tomar
// referencias: muchas pruebas escriben CHECK_EQ(Funcion(...).campo, valor), y
// "campo" es miembro de un temporal que se destruye al terminar esa expresión.
// Con una referencia (const auto& va_) quedaría colgando y ToText leería memoria
// liberada. La copia cuesta poco en pruebas y evalúa cada argumento una sola
// vez. Por eso también los tipos deben ser copiables.
// (No se pueden poner comentarios // dentro de la macro: cada línea termina en
// "\" y el comentario se tragaría el resto; el comentario interno usa /* */.)
#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        /* copias: (a) puede ser miembro de un temporal que muere al terminar la expresión */            \
        const auto va_ = (a);                                                                              \
        const auto vb_ = (b);                                                                              \
        if (!(va_ == vb_))                                                                                 \
            testing::ReportFailure(__FILE__, __LINE__,                                                     \
                                   std::string(#a " == " #b "  (") + testing::ToText(va_) + " vs " +       \
                                       testing::ToText(vb_) + ")");                                        \
    } while (0)
