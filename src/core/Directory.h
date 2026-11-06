#pragma once
// Consulta del carnet de los estudiantes en el Active Directory del dominio
// (D56). El carnet está en el atributo `employeeID` de la cuenta (en el
// dominio estudiantes.tec.cr; `employeeNumber` es la cédula y no se usa) y la
// cuenta se busca por `sAMAccountName` = el usuario del trabajo de impresión.
//
// REGLA: el Active Directory es SOLO DE LECTURA para esta app. Aquí solo se
// conecta (bind con las credenciales del operador) y se busca (ldap_search);
// nunca se agrega, modifica, renombra ni borra nada. No agregar llamadas como
// ldap_add / ldap_modify / ldap_delete / ldap_rename ni IADs::Put/SetInfo.
//
// La conexión es por LDAP (wldap32) con el controlador de dominio del equipo
// y las credenciales de Windows del operador (Negotiate, firmado). Las
// llamadas son de red: usarlas solo desde un hilo de fondo (CarnetService).

#include <map>
#include <string>
#include <vector>

namespace cola::core {

// Atributo del directorio con el carnet.
inline constexpr wchar_t kCarnetAttribute[] = L"employeeID";

// Nombre de cuenta (sAMAccountName) a partir del usuario de un trabajo:
// "ESTUDIANTES\\chrodriguez" o "chrodriguez@estudiantec.cr" -> "chrodriguez".
// Vacío si no parece una cuenta (vacío o con caracteres que no admite).
std::wstring AccountName(const std::wstring& user);

// Escapa un valor para un filtro LDAP (RFC 4515): * ( ) \ y NUL.
std::wstring LdapEscape(const std::wstring& value);

// Filtro que busca esas cuentas (de personas) de una sola vez.
std::wstring CarnetFilter(const std::vector<std::wstring>& accounts);

// Conexión de solo lectura al directorio del dominio.
class DirectoryReader {
public:
    DirectoryReader() = default;
    ~DirectoryReader();
    DirectoryReader(const DirectoryReader&) = delete;
    DirectoryReader& operator=(const DirectoryReader&) = delete;

    // Busca el carnet de las cuentas (sAMAccountName). Devuelve true si la
    // consulta se hizo: `found` trae cuenta (en minúsculas) -> carnet de las
    // que existen y tienen carnet; las demás no están en el directorio o no
    // tienen el atributo. false = no se pudo consultar (equipo fuera del
    // dominio, controlador inaccesible...), con el motivo en `error`.
    bool FindCarnets(const std::vector<std::wstring>& accounts, std::map<std::wstring, std::wstring>& found,
                     std::wstring& error);

private:
    bool Connect(std::wstring& error);
    void Disconnect();

    void* ld_ = nullptr;          // LDAP* (wldap32)
    std::wstring baseDn_;         // defaultNamingContext del dominio
};

}  // namespace cola::core
