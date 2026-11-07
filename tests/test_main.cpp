// Ejecutor de pruebas unitarias. Uso: cola_tests.exe (o ctest en la carpeta build).
//
// No contiene pruebas: ejecuta todas las que los test_*.cpp registraron con
// TEST(...) (ver TestFramework.h) y resume el resultado. scripts/build.ps1 lo
// corre después de compilar y se detiene si devuelve 1. Los mensajes de cada
// CHECK fallido salen por stderr; la línea [ OK ] / [FALLA] de cada prueba y el
// resumen final, por stdout.

#include <windows.h>
#include <objbase.h>

#include "TestFramework.h"

// wmain (y no main) como el resto del proyecto, que trabaja en UTF-16 con la API
// "W" de Windows.
int wmain() {
    // COM inicializado en modo apartamento (STA) como en la app: algunas pruebas
    // usan componentes COM, por ejemplo el XPS Object Model para leer el
    // PrintTicket de un XPS (D25), que no funcionan sin CoInitializeEx.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int failedTests = 0;
    for (const auto& test : testing::Registry()) {
        // FailureCount es global: si aumentó durante el cuerpo, al menos un
        // CHECK de esta prueba falló.
        int before = testing::FailureCount();
        test.body();
        bool ok = testing::FailureCount() == before;
        if (!ok) ++failedTests;
        std::cout << (ok ? "[ OK ] " : "[FALLA] ") << test.name << "\n";
    }
    std::cout << "\n" << testing::Registry().size() << " pruebas, " << failedTests << " fallidas\n";
    CoUninitialize();
    // 0 = todo en verde; 1 = hubo fallos (lo interpretan ctest y build.ps1).
    return failedTests == 0 ? 0 : 1;
}
