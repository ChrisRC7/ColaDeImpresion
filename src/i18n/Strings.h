#pragma once
// Textos traducibles de la interfaz (español / inglés).
// Los textos viven en StringTable.inl; aquí solo está la API.
//
// Técnica "X-macro" (D16): StringTable.inl es una lista de X(Id, es, en) que
// se incluye varias veces con distintas definiciones de X. Aquí genera el
// enum S (un valor por texto); en Strings.cpp genera los dos arreglos de
// textos en el mismo orden. Así enum y textos nunca se desalinean, y un texto
// que falte en un idioma es un error de compilación.
// El idioma es global para todo el proceso; cambiarlo desde el menú surte
// efecto en el próximo repintado (cada pantalla vuelve a llamar a Tr).

namespace cola::i18n {

// Idiomas disponibles; español es el predeterminado (público del TEC).
enum class Language { Spanish, English };

// Identificador de cada texto de la interfaz (S::AppTitle, S::ColUser...).
// Count_ no es un texto: es la cantidad.
enum class S : int {
#define X(id, es, en) id,
#include "i18n/StringTable.inl"
#undef X
    Count_
};

// Cambia / consulta el idioma activo. Se usan solo desde el hilo de la UI
// (variable global sin sincronización).
void SetLanguage(Language language);
Language GetLanguage();
Language LanguageFromCode(const wchar_t* code);  // "es" | "en"
                                                 // (cualquier otro valor o nullptr: español)
// Código para config.json ("es" | "en").
const wchar_t* LanguageCode(Language language);

// Texto en el idioma actual.
// El puntero apunta a un literal estático: es válido siempre y no se libera.
// Un id fuera de rango devuelve "".
const wchar_t* Tr(S id);

}  // namespace cola::i18n
