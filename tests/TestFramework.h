#pragma once
// Mini framework de pruebas sin dependencias.
//
//   TEST(Nombre) { CHECK(expr); CHECK_EQ(a, b); }
//
// Las pruebas se registran solas; test_main.cpp las ejecuta todas.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& FailureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { Registry().push_back({name, std::move(body)}); }
};

inline void ReportFailure(const char* file, int line, const std::string& message) {
    ++FailureCount();
    std::cerr << "  FALLO " << file << ":" << line << ": " << message << "\n";
}

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

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST(name)                                                                         \
    static void TEST_CONCAT(test_, name)();                                                \
    static testing::Registrar TEST_CONCAT(registrar_, name)(#name, TEST_CONCAT(test_, name)); \
    static void TEST_CONCAT(test_, name)()

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) testing::ReportFailure(__FILE__, __LINE__, #expr); \
    } while (0)

#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        auto&& va_ = (a);                                                                                  \
        auto&& vb_ = (b);                                                                                  \
        if (!(va_ == vb_))                                                                                 \
            testing::ReportFailure(__FILE__, __LINE__,                                                     \
                                   std::string(#a " == " #b "  (") + testing::ToText(va_) + " vs " +       \
                                       testing::ToText(vb_) + ")");                                        \
    } while (0)
