// Implementación de la tabla de textos (ver Strings.h): dos arreglos
// generados con la X-macro de StringTable.inl, indexados por S.

#include "i18n/Strings.h"

#include <cwchar>

namespace cola::i18n {

namespace {
// Idioma activo (lo fija Application/MainWindow según config.json).
Language g_language = Language::Spanish;

// Textos en español, en el mismo orden que el enum S (la X toma la 2.ª columna).
const wchar_t* const kSpanish[] = {
#define X(id, es, en) es,
#include "i18n/StringTable.inl"
#undef X
};

// Textos en inglés (3.ª columna).
const wchar_t* const kEnglish[] = {
#define X(id, es, en) en,
#include "i18n/StringTable.inl"
#undef X
};

// Garantía en compilación de que hay un texto por id en cada idioma.
static_assert(sizeof(kSpanish) / sizeof(kSpanish[0]) == static_cast<size_t>(S::Count_));
static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == static_cast<size_t>(S::Count_));
}  // namespace

void SetLanguage(Language language) { g_language = language; }
Language GetLanguage() { return g_language; }

Language LanguageFromCode(const wchar_t* code) {
    return (code && std::wcscmp(code, L"en") == 0) ? Language::English : Language::Spanish;
}

const wchar_t* LanguageCode(Language language) { return language == Language::English ? L"en" : L"es"; }

const wchar_t* Tr(S id) {
    auto index = static_cast<size_t>(id);
    // Protección ante un valor fuera del enum (p. ej. un cast erróneo).
    if (index >= static_cast<size_t>(S::Count_)) return L"";
    return g_language == Language::English ? kEnglish[index] : kSpanish[index];
}

}  // namespace cola::i18n
