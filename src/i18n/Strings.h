#pragma once
// Textos traducibles de la interfaz (español / inglés).
// Los textos viven en StringTable.inl; aquí solo está la API.

namespace cola::i18n {

enum class Language { Spanish, English };

enum class S : int {
#define X(id, es, en) id,
#include "i18n/StringTable.inl"
#undef X
    Count_
};

void SetLanguage(Language language);
Language GetLanguage();
Language LanguageFromCode(const wchar_t* code);  // "es" | "en"
const wchar_t* LanguageCode(Language language);

// Texto en el idioma actual.
const wchar_t* Tr(S id);

}  // namespace cola::i18n
