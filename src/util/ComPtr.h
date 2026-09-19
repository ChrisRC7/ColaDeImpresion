#pragma once
// Puntero inteligente mínimo para interfaces COM (evita depender de WRL/ATL,
// que no están disponibles en todas las toolchains, p. ej. MinGW).

#include <unknwn.h>

#include <utility>

namespace cola::util {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& other) : ptr_(other.ptr_) {
        if (ptr_) ptr_->AddRef();
    }
    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    ~ComPtr() { Reset(); }

    ComPtr& operator=(ComPtr other) noexcept {
        std::swap(ptr_, other.ptr_);
        return *this;
    }

    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    // Para pasar como parámetro de salida: libera el valor previo.
    T** Put() {
        Reset();
        return &ptr_;
    }
    void** PutVoid() { return reinterpret_cast<void**>(Put()); }

private:
    T* ptr_ = nullptr;
};

}  // namespace cola::util
