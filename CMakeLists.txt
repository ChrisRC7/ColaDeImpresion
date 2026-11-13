# Compilación de Cola de Impresión (C++20 + Win32, sin frameworks de UI).
# Normalmente se usa a través de scripts/build.ps1, que busca MinGW (WinLibs),
# usa Ninja si está instalado, compila, corre las pruebas y, con -Package,
# arma dist\. También compila con MSVC (ramas if(MSVC) más abajo).
# Al subir la versión hay que cambiarla aquí, en res/app.rc y en
# installer/ColaDeImpresion.iss (ver docs/GUIA_IA.md).
cmake_minimum_required(VERSION 3.20)

# RC: compila res/app.rc (ícono, manifiesto y datos de versión del .exe).
project(ColaDeImpresion VERSION 0.5.18 LANGUAGES CXX RC)

# C++20 estricto, sin extensiones del compilador (gnu++20).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Con MSVC, runtime estático (/MT): el .exe no depende de MSVCP140/VCRUNTIME140,
# igual que con MinGW (-static). Las PCs del laboratorio pueden no tenerlas.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Generadores de una sola configuración (MinGW/Ninja): Release si no se pidió
# otra. Los multiconfiguración (Visual Studio) eligen con --config.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

option(COLA_BUILD_TESTS "Compilar las pruebas unitarias" ON)

# ---------------------------------------------------------------------------
# Biblioteca núcleo: todo excepto main.cpp, para poder enlazarla en las pruebas.
# Cada carpeta de src/ es un módulo (ver docs/CONTEXTO.md).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE COLA_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/util/*.cpp
    ${CMAKE_SOURCE_DIR}/src/i18n/*.cpp
    ${CMAKE_SOURCE_DIR}/src/config/*.cpp
    ${CMAKE_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/*.cpp
    ${CMAKE_SOURCE_DIR}/src/input/*.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/*.cpp
    ${CMAKE_SOURCE_DIR}/src/app/*.cpp
)

# CONFIGURE_DEPENDS: un .cpp nuevo se detecta al compilar, sin reconfigurar a mano.
add_library(cola_core STATIC ${COLA_CORE_SOURCES})
# Los #include son relativos a src/ (p. ej. "core/Spooler.h").
target_include_directories(cola_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# PUBLIC: también las heredan el ejecutable, las pruebas y las herramientas.
#  - UNICODE/_UNICODE: las API de Win32 en su versión W (wchar_t).
#  - WIN32_LEAN_AND_MEAN/NOMINMAX: windows.h más liviano y sin las macros
#    min/max que chocan con std::min/std::max.
#  - _WIN32_WINNT/WINVER/NTDDI_VERSION: API de Windows 10 en adelante.
#  - COLA_VERSION_STRING: la versión del proyecto como literal L"..."
#    (la muestra el diálogo "Acerca de").
target_compile_definitions(cola_core PUBLIC
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006
    COLA_VERSION_STRING=L"${PROJECT_VERSION}"
)
# Bibliotecas del sistema: winspool (spooler), gdi32/gdiplus/msimg32 (dibujo),
# comctl32/comdlg32/shell32/shlwapi (controles, "Guardar como", shell),
# ole32/oleaut32/uuid/windowscodecs (COM y WIC para imágenes), dwmapi/uxtheme
# (modo oscuro), advapi32 (registro, inicio con Windows), ws2_32 (resolver
# nombres de servidor) y wldap32 (Active Directory, solo lectura).
target_link_libraries(cola_core PUBLIC
    winspool user32 gdi32 gdiplus comctl32 comdlg32 shell32 shlwapi ole32 oleaut32
    uuid dwmapi uxtheme windowscodecs advapi32 msimg32 ws2_32 wldap32
)

# Advertencias al máximo razonable: la regla es compilar sin warnings.
# /utf-8: los fuentes están en UTF-8 (textos con tildes y símbolos).
if(MSVC)
    target_compile_options(cola_core PUBLIC /W4 /utf-8 /permissive- /EHsc)
else()
    target_compile_options(cola_core PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type)
    # Enlazado estático del runtime para que el .exe no dependa de DLLs de MinGW.
    # -municode: el punto de entrada es wWinMain/wmain. -s (solo Release)
    # quita los símbolos para achicar el .exe.
    target_link_options(cola_core PUBLIC -static -static-libgcc -static-libstdc++ -municode
                        $<$<CONFIG:Release>:-s>)
endif()

# ---------------------------------------------------------------------------
# Ejecutable principal
# ---------------------------------------------------------------------------
# WIN32: subsistema gráfico (sin ventana de consola).
add_executable(ColaDeImpresion WIN32
    src/main.cpp
    res/app.rc
)
target_link_libraries(ColaDeImpresion PRIVATE cola_core)
if(MSVC)
    # El manifiesto ya se incluye desde app.rc
    target_link_options(ColaDeImpresion PRIVATE /MANIFEST:NO)
else()
    # MinGW enlaza su manifiesto predeterminado (default-manifest.o) y, con el
    # de app.rc, el .exe quedaba con dos manifiestos ("multiple non-default
    # manifests"). Un default-manifest.o vacío en una carpeta de -B lo reemplaza
    # (ld lo lee como un script de enlazador sin instrucciones).
    set(COLA_NO_MANIFEST_DIR ${CMAKE_BINARY_DIR}/sin-manifiesto)
    file(WRITE ${COLA_NO_MANIFEST_DIR}/default-manifest.o "/* sin manifiesto predeterminado */\n")
    target_link_options(ColaDeImpresion PRIVATE "-B${COLA_NO_MANIFEST_DIR}/")
endif()

# ---------------------------------------------------------------------------
# Pruebas unitarias (lógica pura: JSON, atajos, formato, spool EMF, tema...)
# ---------------------------------------------------------------------------
if(COLA_BUILD_TESTS)
    enable_testing()
    file(GLOB COLA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
    # Un solo ejecutable con todas las pruebas (tests/TestFramework.h);
    # build.ps1 lo corre después de compilar y falla si alguna no pasa.
    add_executable(cola_tests ${COLA_TEST_SOURCES})
    target_link_libraries(cola_tests PRIVATE cola_core)
    add_test(NAME cola_tests COMMAND cola_tests)
endif()

# ---------------------------------------------------------------------------
# Herramienta de desarrollo: genera trabajos de impresión de prueba
# ---------------------------------------------------------------------------
option(COLA_BUILD_TOOLS "Compilar herramientas de desarrollo" ON)
if(COLA_BUILD_TOOLS)
    # cola_testjob no usa cola_core: imprime con GDI directamente, así que
    # declara sus propias definiciones y bibliotecas.
    add_executable(cola_testjob tools/testjob.cpp)
    target_compile_definitions(cola_testjob PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(cola_testjob PRIVATE winspool gdi32 user32)
    if(NOT MSVC)
        target_link_options(cola_testjob PRIVATE -municode -static)
    endif()

    # Inspección de trabajos (ficha de configuración, páginas/copias/caras y
    # evaluación de la vista previa) con el mismo código que la app.
    add_executable(cola_jobinspect tools/jobinspect.cpp)
    target_link_libraries(cola_jobinspect PRIVATE cola_core)
endif()
