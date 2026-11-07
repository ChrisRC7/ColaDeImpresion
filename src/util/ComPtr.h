#pragma once
// Puntero inteligente mínimo para interfaces COM (evita depender de WRL/ATL,
// que no están disponibles en todas las toolchains, p. ej. MinGW).
//
// Semántica: el ComPtr es dueño de UNA referencia de la interfaz. Copiar suma
// una referencia (AddRef), mover la transfiere sin tocar el contador y
// destruir/Reset la libera (Release). Se usa con WIC, el Shell (miniaturas) y
// el XPS Object Model (módulo preview). No es seguro compartir una misma
// instancia entre hilos sin sincronización (igual que un puntero crudo).

#include <unknwn.h>

#include <utility>

namespace cola::util {

// T: cualquier interfaz que derive de IUnknown (IWICImagingFactory, IXpsOM...).
template <typename T>
class ComPtr {
public:
    // Vacío (nullptr).
    ComPtr() = default;
    // Copia: ambos ComPtr quedan con su propia referencia.
    ComPtr(const ComPtr& other) : ptr_(other.ptr_) {
        if (ptr_) ptr_->AddRef();
    }
    // Movimiento: toma la referencia del otro y lo deja vacío.
    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    // Libera la referencia propia, si hay.
    ~ComPtr() { Reset(); }

    // Asignación "copy-and-swap": `other` llega por valor (copiado o movido),
    // se intercambia y su destructor libera el valor anterior de *this. Así un
    // solo operador sirve para copia y movimiento y es seguro ante autoasignación.
    ComPtr& operator=(ComPtr other) noexcept {
        std::swap(ptr_, other.ptr_);
        return *this;
    }

    // Libera la referencia (Release) y deja el puntero en nullptr.
    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    // Puntero crudo SIN transferir la referencia (no llamar Release sobre él).
    T* Get() const { return ptr_; }
    // Acceso a los métodos de la interfaz; el llamador debe comprobar que no
    // esté vacío.
    T* operator->() const { return ptr_; }
    // true si contiene una interfaz.
    explicit operator bool() const { return ptr_ != nullptr; }

    // Para pasar como parámetro de salida: libera el valor previo.
    // Ej.: factory->CreateDecoder(..., decoder.Put()); la función COM escribe
    // un puntero que ya trae su referencia, y este ComPtr pasa a ser su dueño.
    T** Put() {
        Reset();
        return &ptr_;
    }
    // Igual que Put() pero como void** (CoCreateInstance, QueryInterface).
    void** PutVoid() { return reinterpret_cast<void**>(Put()); }

private:
    // Interfaz poseída (una referencia) o nullptr.
    T* ptr_ = nullptr;
};

}  // namespace cola::util
