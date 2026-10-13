cmake_minimum_required(VERSION 3.20)

project(ColaDeImpresion VERSION 0.5.7 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Con MSVC, runtime estático (/MT): el .exe no depende de MSVCP140/VCRUNTIME140,
# igual que con MinGW (-static). Las PCs del laboratorio pueden no tenerlas.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

add_library(cola_core STATIC ${COLA_CORE_SOURCES})
target_include_directories(cola_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(cola_core PUBLIC
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006
    COLA_VERSION_STRING=L"${PROJECT_VERSION}"
)
target_link_libraries(cola_core PUBLIC
    winspool user32 gdi32 gdiplus comctl32 comdlg32 shell32 shlwapi ole32 oleaut32
    uuid dwmapi uxtheme windowscodecs advapi32 msimg32 ws2_32
)

if(MSVC)
    target_compile_options(cola_core PUBLIC /W4 /utf-8 /permissive- /EHsc)
else()
    target_compile_options(cola_core PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type)
    # Enlazado estático del runtime para que el .exe no dependa de DLLs de MinGW.
    target_link_options(cola_core PUBLIC -static -static-libgcc -static-libstdc++ -municode
                        $<$<CONFIG:Release>:-s>)
endif()

# ---------------------------------------------------------------------------
# Ejecutable principal
# ---------------------------------------------------------------------------
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
    add_executable(cola_tests ${COLA_TEST_SOURCES})
    target_link_libraries(cola_tests PRIVATE cola_core)
    add_test(NAME cola_tests COMMAND cola_tests)
endif()

# ---------------------------------------------------------------------------
# Herramienta de desarrollo: genera trabajos de impresión de prueba
# ---------------------------------------------------------------------------
option(COLA_BUILD_TOOLS "Compilar herramientas de desarrollo" ON)
if(COLA_BUILD_TOOLS)
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
