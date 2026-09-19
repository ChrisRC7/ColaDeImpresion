// Ejecutor de pruebas unitarias. Uso: cola_tests.exe (o ctest en la carpeta build).

#include <windows.h>
#include <objbase.h>

#include "TestFramework.h"

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int failedTests = 0;
    for (const auto& test : testing::Registry()) {
        int before = testing::FailureCount();
        test.body();
        bool ok = testing::FailureCount() == before;
        if (!ok) ++failedTests;
        std::cout << (ok ? "[ OK ] " : "[FALLA] ") << test.name << "\n";
    }
    std::cout << "\n" << testing::Registry().size() << " pruebas, " << failedTests << " fallidas\n";
    CoUninitialize();
    return failedTests == 0 ? 0 : 1;
}
