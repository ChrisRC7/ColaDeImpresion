// Implementación de la consulta del carnet en el Active Directory (D56).
// SOLO LECTURA: este archivo únicamente se conecta (bind) y busca (search).
// Ver la regla completa en core/Directory.h y en CLAUDE.md / AGENTS.md.

#include "core/Directory.h"

#include <windows.h>
#include <winldap.h>  // API LDAP de Windows (wldap32), enlazada en CMakeLists.txt

#include <algorithm>

#include "util/Strings.h"

namespace cola::core {

namespace {

// Cuentas por consulta: varias cuentas se buscan juntas con un filtro OR
// "(|(sAMAccountName=a)(sAMAccountName=b)...)"; con lotes de 40 el filtro
// queda corto y una cola con muchos usuarios nuevos se resuelve en pocas idas
// y vueltas al controlador de dominio.
constexpr size_t kBatch = 40;
// Segundos máximos para conectar al controlador y para cada búsqueda: si el
// directorio no responde, el hilo de fondo no queda bloqueado mucho tiempo.
constexpr long kTimeoutSeconds = 10;

// ld_ se guarda como void* en el encabezado para no exponer <winldap.h> a
// quien lo incluya; aquí se convierte de vuelta al tipo real.
LDAP* AsLdap(void* p) { return static_cast<LDAP*>(p); }

// Texto legible de un código de error LDAP para app.log ("LDAP 81: Server Down").
std::wstring LdapErrorText(ULONG code) {
    const wchar_t* text = ldap_err2stringW(code);
    return util::Format(L"LDAP %lu: %ls", static_cast<unsigned long>(code), text ? text : L"?");
}

// Primer valor de un atributo de la entrada ("" si no lo tiene). Los atributos
// LDAP pueden tener varios valores; employeeID y sAMAccountName tienen uno.
std::wstring FirstValue(LDAP* ld, LDAPMessage* entry, const wchar_t* attribute) {
    // La API pide un PWSTR (no const): se pasa una copia modificable.
    std::wstring name(attribute);
    PWCHAR* values = ldap_get_valuesW(ld, entry, name.data());
    std::wstring value;
    if (values) {
        if (values[0]) value = values[0];
        ldap_value_freeW(values);  // la lista la reserva wldap32: se libera con su función
    }
    return value;
}

}  // namespace

std::wstring AccountName(const std::wstring& user) {
    std::wstring name = util::Trim(user);
    // El spooler suele dar solo el usuario ("chrodriguez"), pero según cómo se
    // envió el trabajo puede venir con el dominio delante ("ESTUDIANTES\usuario")
    // o en formato UPN ("usuario@estudiantec.cr"): se deja solo la cuenta.
    if (auto slash = name.rfind(L'\\'); slash != std::wstring::npos) name = name.substr(slash + 1);
    if (auto at = name.find(L'@'); at != std::wstring::npos) name = name.substr(0, at);
    name = util::Trim(name);
    // Un sAMAccountName de usuario tiene a lo sumo 20 caracteres; 64 deja margen
    // y descarta textos que claramente no son una cuenta.
    if (name.empty() || name.size() > 64) return {};
    // Caracteres que un sAMAccountName no admite (y caracteres de control): un
    // usuario así no puede estar en el directorio, así que no se consulta.
    for (wchar_t c : name)
        if (c < 0x20 || std::wstring_view(L"\"/\\[]:;|=,+*?<>").find(c) != std::wstring_view::npos) return {};
    return name;
}

std::wstring LdapEscape(const std::wstring& value) {
    // RFC 4515: dentro de un filtro, estos caracteres tienen significado
    // especial (comodín, agrupación, escape) y se escriben como \XX en hexadecimal.
    // Así un nombre como "a(b)" se busca literalmente y no altera el filtro.
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t c : value) {
        switch (c) {
            case L'*': out += L"\\2a"; break;
            case L'(': out += L"\\28"; break;
            case L')': out += L"\\29"; break;
            case L'\\': out += L"\\5c"; break;
            case L'\0': out += L"\\00"; break;
            default: out += c;
        }
    }
    return out;
}

std::wstring CarnetFilter(const std::vector<std::wstring>& accounts) {
    // Una condición por cuenta; con más de una se agrupan con OR "(|...)".
    std::wstring names;
    for (const auto& account : accounts) names += L"(sAMAccountName=" + LdapEscape(account) + L")";
    if (accounts.size() > 1) names = L"(|" + names + L")";
    // objectCategory=person + objectClass=user: solo cuentas de personas (no
    // equipos ni grupos, que también tienen sAMAccountName).
    return L"(&(objectCategory=person)(objectClass=user)" + names + L")";
}

DirectoryReader::~DirectoryReader() { Disconnect(); }

void DirectoryReader::Disconnect() {
    // ldap_unbind cierra la conexión y libera el LDAP* (no modifica el directorio).
    if (ld_) ldap_unbind(AsLdap(ld_));
    ld_ = nullptr;
    baseDn_.clear();
}

bool DirectoryReader::Connect(std::wstring& error) {
    if (ld_) return true;  // conexión ya abierta en una consulta anterior
    // Sin nombre de servidor: wldap32 usa el localizador de controladores de
    // dominio y se conecta a uno del dominio del equipo (estudiantes.tec.cr).
    LDAP* ld = ldap_initW(nullptr, LDAP_PORT);
    if (!ld) {
        error = LdapErrorText(LdapGetLastError());
        return false;
    }
    // LDAP v3 (el que usa Active Directory).
    ULONG version = LDAP_VERSION3;
    ldap_set_optionW(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    // Sin seguir referencias a otros dominios: solo interesa el propio, y
    // seguirlas puede demorar mucho si el otro servidor no responde.
    ldap_set_optionW(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);
    // Firmado: los controladores que exigen firma LDAP no aceptan el bind sin ella.
    ldap_set_optionW(ld, LDAP_OPT_SIGN, LDAP_OPT_ON);
    // Tiempo máximo que el servidor dedica a cada búsqueda.
    ULONG limit = kTimeoutSeconds;
    ldap_set_optionW(ld, LDAP_OPT_TIMELIMIT, &limit);

    l_timeval timeout{kTimeoutSeconds, 0};
    ULONG rc = ldap_connect(ld, &timeout);
    // Autenticación con las credenciales de Windows del operador (Negotiate:
    // Kerberos/NTLM, sin pedir contraseña). El bind solo identifica al
    // operador; no cambia nada en el directorio.
    if (rc == LDAP_SUCCESS) rc = ldap_bind_sW(ld, nullptr, nullptr, LDAP_AUTH_NEGOTIATE);
    if (rc != LDAP_SUCCESS) {
        error = LdapErrorText(rc);
        ldap_unbind(ld);
        return false;
    }

    // Raíz del dominio donde buscar (p. ej. DC=estudiantes,DC=tec,DC=cr): se lee
    // del RootDSE (base vacía, alcance BASE), que todo controlador publica. Así
    // no hay nombres de dominio fijos en el código.
    // Las variables son std::wstring porque la API pide PWSTR (no const).
    std::wstring base, filter = L"(objectClass=*)", attribute = L"defaultNamingContext";
    PWCHAR attributes[] = {attribute.data(), nullptr};
    LDAPMessage* result = nullptr;
    rc = ldap_search_ext_sW(ld, base.data(), LDAP_SCOPE_BASE, filter.data(), attributes, 0, nullptr, nullptr,
                            &timeout, 1, &result);
    std::wstring dn;
    if (rc == LDAP_SUCCESS)
        if (LDAPMessage* entry = ldap_first_entry(ld, result)) dn = FirstValue(ld, entry, L"defaultNamingContext");
    // Aun con error, la búsqueda puede haber reservado un resultado: se libera siempre.
    if (result) ldap_msgfree(result);
    if (dn.empty()) {
        error = rc != LDAP_SUCCESS ? LdapErrorText(rc) : std::wstring(L"defaultNamingContext vacío");
        ldap_unbind(ld);
        return false;
    }
    // Conexión lista: se conserva para las siguientes consultas.
    ld_ = ld;
    baseDn_ = dn;
    return true;
}

bool DirectoryReader::FindCarnets(const std::vector<std::wstring>& accounts,
                                  std::map<std::wstring, std::wstring>& found, std::wstring& error) {
    if (accounts.empty()) return true;  // nada que buscar
    if (!Connect(error)) return false;
    LDAP* ld = AsLdap(ld_);
    // Solo se piden los dos atributos necesarios: la cuenta (para saber de quién
    // es cada resultado) y el carnet. Nada más del usuario viaja por la red.
    std::wstring samName = L"sAMAccountName", carnetName = kCarnetAttribute;
    PWCHAR attributes[] = {samName.data(), carnetName.data(), nullptr};
    l_timeval timeout{kTimeoutSeconds, 0};

    // Las cuentas se buscan por lotes de kBatch (un filtro OR por lote).
    for (size_t start = 0; start < accounts.size(); start += kBatch) {
        const std::vector<std::wstring> batch(accounts.begin() + start,
                                              accounts.begin() + std::min(accounts.size(), start + kBatch));
        std::wstring filter = CarnetFilter(batch);
        LDAPMessage* result = nullptr;
        // Solo búsqueda: ninguna operación de escritura sobre el directorio.
        // Alcance SUBTREE desde la raíz del dominio: los estudiantes están en
        // varias unidades organizativas (OU=A02,OU=EstudiantesActivos...).
        // Límite de resultados: el doble del lote, por si una cuenta se repitiera.
        ULONG rc = ldap_search_ext_sW(ld, baseDn_.data(), LDAP_SCOPE_SUBTREE, filter.data(), attributes, 0, nullptr,
                                      nullptr, &timeout, static_cast<ULONG>(batch.size() * 2), &result);
        // LDAP_SIZELIMIT_EXCEEDED trae resultados parciales válidos: se usan.
        if (rc != LDAP_SUCCESS && rc != LDAP_SIZELIMIT_EXCEEDED) {
            if (result) ldap_msgfree(result);
            error = LdapErrorText(rc);
            Disconnect();  // la conexión pudo caerse: la próxima vez se vuelve a conectar
            return false;
        }
        // Recorre las cuentas encontradas. Las que no aparecen no existen en el
        // directorio (o no son personas) y quedan fuera de `found`.
        for (LDAPMessage* entry = ldap_first_entry(ld, result); entry; entry = ldap_next_entry(ld, entry)) {
            // En minúsculas: el directorio no distingue mayúsculas y el spooler
            // puede dar "ChRodriguez" o "chrodriguez" para la misma cuenta.
            const std::wstring account = util::ToLower(FirstValue(ld, entry, L"sAMAccountName"));
            const std::wstring carnet = util::Trim(FirstValue(ld, entry, kCarnetAttribute));
            if (!account.empty() && !carnet.empty()) found[account] = carnet;
        }
        if (result) ldap_msgfree(result);
    }
    return true;
}

}  // namespace cola::core
