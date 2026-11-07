// Análisis de paquetes XPS / OpenXPS con el XPS Object Model y lectura de
// PrintTickets (Print Schema). Ver XpsPackage.h.
//
// Ejemplo de PrintTicket (lo que interpreta ApplyPrintTicket):
//   <psf:PrintTicket xmlns:psf="..." xmlns:psk="...">
//     <psf:Feature name="psk:PageMediaSize">
//       <psf:Option name="psk:ISOA4">
//         <psf:ScoredProperty name="psk:MediaSizeWidth">
//           <psf:Value xsi:type="xsd:integer">210000</psf:Value>  (micrones)
//     <psf:ParameterInit name="psk:JobCopiesAllDocuments">
//       <psf:Value xsi:type="xsd:integer">2</psf:Value>
#include "preview/XpsPackage.h"

#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <xpsobjectmodel.h>
#include <xpsobjectmodel_1.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "util/ComPtr.h"
#include "util/Strings.h"

namespace cola::preview {

namespace {

using util::ComPtr;

// Nombre local de un nombre calificado ("psk:ISOA4" -> "ISOA4").
std::string Local(const std::string& qualified) {
    size_t colon = qualified.rfind(':');
    return colon == std::string::npos ? qualified : qualified.substr(colon + 1);
}

// Valor de un atributo entre comillas dobles dentro del texto de una
// etiqueta (sin '<' ni '>'): Attribute("Option name=\"psk:ISOA4\"", "name")
// -> "psk:ISOA4". "" si no está. No entiende comillas simples (el Print
// Schema que generan los drivers usa dobles).
std::string Attribute(const std::string& tag, const char* attr) {
    std::string key = std::string(attr) + "=\"";
    size_t at = tag.find(key);
    if (at == std::string::npos) return "";
    at += key.size();
    size_t end = tag.find('"', at);
    return end == std::string::npos ? "" : tag.substr(at, end - at);
}

// Elemento abierto en la pila del recorrido del PrintTicket.
struct Node {
    std::string kind;  // Feature, Option, ScoredProperty, ParameterInit, Property, Value...
    std::string name;  // valor del atributo name, sin prefijo
};

// Busca hacia arriba en la pila el elemento más cercano de un tipo.
const Node* Nearest(const std::vector<Node>& stack, const char* kind) {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        if (it->kind == kind) return &*it;
    return nullptr;
}

// Nombre de opción de PageMediaSize del Print Schema (sin prefijo) ->
// código DMPAPER. 0 si no está en la tabla (se muestra solo el nombre, y
// las medidas pueden salir de MediaSizeWidth/Height).
short PaperCodeFromOption(const std::string& option) {
    struct Entry {
        const char* option;
        short code;
    };
    static const Entry kOptions[] = {
        {"NorthAmericaLetter", DMPAPER_LETTER},  {"NorthAmericaLegal", DMPAPER_LEGAL},
        {"NorthAmericaExecutive", DMPAPER_EXECUTIVE}, {"NorthAmericaTabloid", DMPAPER_TABLOID},
        {"ISOA3", DMPAPER_A3},  {"ISOA4", DMPAPER_A4}, {"ISOA5", DMPAPER_A5}, {"ISOA6", DMPAPER_A6},
        {"JISB4", DMPAPER_B4},  {"JISB5", DMPAPER_B5},
    };
    for (const auto& e : kOptions)
        if (option == e.option) return e.code;
    return 0;
}

// Se llama por cada <Option name="..."> con el nombre de su <Feature>.
// Traduce las opciones del Print Schema a valores DEVMODE:
//   PageMediaSize        -> papel (nombre y medidas)
//   PageOrientation      -> orientación (Reverse* se trata igual que la normal)
//   *Duplex*             -> una cara / borde largo / borde corto
//   PageOutputColor      -> color / escala de grises
//   *Collate*            -> intercalado
// 'any' pasa a true si la característica es una de estas (aunque la opción
// concreta no se reconozca).
void OnOption(const std::string& feature, const std::string& option, core::ContentFacts& facts, bool& any) {
    if (feature == "PageMediaSize") {
        facts.paperName.Set(util::FromUtf8(option));
        if (const core::PaperInfo* p = core::FindPaper(PaperCodeFromOption(option))) {
            facts.paperWidth.Set(p->width);
            facts.paperLength.Set(p->length);
        }
        any = true;
    } else if (feature == "PageOrientation") {
        if (option == "Portrait" || option == "ReversePortrait") facts.orientation.Set(DMORIENT_PORTRAIT);
        else if (option == "Landscape" || option == "ReverseLandscape") facts.orientation.Set(DMORIENT_LANDSCAPE);
        any = true;
    } else if (feature == "JobDuplexAllDocumentsContiguously" || feature == "DocumentDuplex") {
        if (option == "OneSided") facts.duplex.Set(DMDUP_SIMPLEX);
        else if (option == "TwoSidedLongEdge") facts.duplex.Set(DMDUP_VERTICAL);
        else if (option == "TwoSidedShortEdge") facts.duplex.Set(DMDUP_HORIZONTAL);
        any = true;
    } else if (feature == "PageOutputColor") {
        if (option == "Color") facts.color.Set(DMCOLOR_COLOR);
        else if (option == "Grayscale" || option == "Monochrome") facts.color.Set(DMCOLOR_MONOCHROME);
        any = true;
    } else if (feature == "DocumentCollate" || feature == "JobCollateAllDocuments") {
        if (option == "Collated") facts.collate.Set(DMCOLLATE_TRUE);
        else if (option == "Uncollated") facts.collate.Set(DMCOLLATE_FALSE);
        any = true;
    }
}

// Se llama por cada <Value>texto</Value>; el significado depende de los
// elementos que lo contienen (pila):
//   ParameterInit JobCopiesAllDocuments     -> copias del trabajo
//   ParameterInit DocumentCopiesAllPages    -> multiplica las copias
//   Feature PageMediaSize / ScoredProperty MediaSizeWidth|Height -> medidas
//   ScoredProperty PagesPerSheet en *NUp*   -> páginas por cara (N-up)
// Los valores no numéricos dan 0 con strtol y se ignoran.
void OnValue(const std::vector<Node>& stack, const std::string& text, core::ContentFacts& facts, bool& any) {
    const Node* param = Nearest(stack, "ParameterInit");
    const Node* scored = Nearest(stack, "ScoredProperty");
    const Node* feature = Nearest(stack, "Feature");
    long value = std::strtol(text.c_str(), nullptr, 10);
    if (param && param->name == "JobCopiesAllDocuments" && value > 0) {
        facts.copies.Set(static_cast<std::uint32_t>(value));
        any = true;
    } else if (param && param->name == "DocumentCopiesAllPages" && value > 1) {
        // Copias del documento dentro de cada copia del trabajo.
        facts.copies.Set((facts.copies.valid ? facts.copies.value : 1u) * static_cast<std::uint32_t>(value));
        any = true;
    } else if (scored && feature && feature->name == "PageMediaSize" && value > 0) {
        // Micrones -> décimas de milímetro.
        // (100 micrones = 0,1 mm; se limita al máximo de un short, como en DEVMODE.)
        short tenths = static_cast<short>(std::min<long>(value / 100, 32767));
        if (scored->name == "MediaSizeWidth") facts.paperWidth.Set(tenths);
        if (scored->name == "MediaSizeHeight") facts.paperLength.Set(tenths);
        any = true;
    } else if (scored && feature && scored->name == "PagesPerSheet" && value > 0 &&
               (feature->name == "JobNUpAllDocumentsContiguously" || feature->name == "DocumentNUp")) {
        facts.pagesPerSheet.Set(static_cast<std::uint32_t>(value));
        any = true;
    }
}

// Lee un IStream completo a una cadena (en bloques de 8 KB, hasta ~4 MB).
std::string ReadAll(IStream* stream) {
    std::string out;
    char buffer[8192];
    ULONG got = 0;
    while (SUCCEEDED(stream->Read(buffer, sizeof(buffer), &got)) && got > 0) {
        out.append(buffer, got);
        if (out.size() > 4 * 1024 * 1024) break;  // un PrintTicket real mide unos KB
    }
    return out;
}

// XML del PrintTicket, o "" si el nivel no tiene ticket (es opcional en
// cada nivel del paquete).
std::string TicketXml(IXpsOMPrintTicketResource* ticket) {
    if (!ticket) return "";
    ComPtr<IStream> stream;
    if (FAILED(ticket->GetStream(stream.Put())) || !stream) return "";
    return ReadAll(stream.Get());
}

}  // namespace

bool ApplyPrintTicket(const std::string& xml, core::ContentFacts& facts) {
    bool any = false;
    std::vector<Node> stack;  // elementos abiertos, del más externo al más interno
    size_t pos = 0;
    // Cada vuelta toma una etiqueta "<...>" completa; 'pos' queda justo
    // después de ella (donde empieza el texto, si lo hay).
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        size_t end = xml.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = xml.substr(pos + 1, end - pos - 1);
        pos = end + 1;
        // Declaración XML (<?xml ...?>) y comentarios (<!-- -->): se saltan.
        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;
        if (tag[0] == '/') {
            std::string kind = Local(tag.substr(1));
            // Cierra hasta el elemento correspondiente (tolera XML algo irregular).
            while (!stack.empty()) {
                bool match = stack.back().kind == kind;
                stack.pop_back();
                if (match) break;
            }
            continue;
        }
        // Etiqueta de apertura: tipo (nombre local del elemento) y atributo name.
        const bool selfClosing = tag.back() == '/';
        size_t nameEnd = tag.find_first_of(" \t\r\n/");
        Node node{Local(tag.substr(0, nameEnd)), Local(Attribute(tag, "name"))};
        if (node.kind == "Option") {
            if (const Node* feature = Nearest(stack, "Feature")) OnOption(feature->name, node.name, facts, any);
        }
        // El texto de <Value> va desde aquí hasta la siguiente etiqueta.
        if (node.kind == "Value" && !selfClosing) {
            size_t textEnd = xml.find('<', pos);
            if (textEnd == std::string::npos) break;
            OnValue(stack, xml.substr(pos, textEnd - pos), facts, any);
        }
        if (!selfClosing) stack.push_back(std::move(node));
    }
    if (any) {
        facts.settingsSource = core::ContentFacts::SettingsSource::PrintTicket;
        facts.settingsDocumented = true;
    }
    return any;
}

bool AnalyzeXpsPackage(const std::uint8_t* data, size_t size, core::ContentFacts& facts, std::wstring* error) {
    auto fail = [&](HRESULT hr, const wchar_t* step) {
        if (error) *error = util::Format(L"%ls: 0x%08lX", step, static_cast<unsigned long>(hr));
        return false;
    };
    // SHCreateMemStream recibe el tamaño como UINT: más de 4 GB no se admite.
    if (!data || size == 0 || size > 0xFFFFFFFFull) return fail(E_INVALIDARG, L"datos");
    ComPtr<IStream> stream;
    *stream.Put() = SHCreateMemStream(data, static_cast<UINT>(size));
    if (!stream) return fail(E_OUTOFMEMORY, L"SHCreateMemStream");

    // IXpsOMObjectFactory1 (Windows 8+) abre también OpenXPS (el formato de
    // los drivers v4); la fábrica original solo entiende MS XPS.
    ComPtr<IXpsOMObjectFactory1> factory;
    HRESULT hr = CoCreateInstance(__uuidof(XpsOMObjectFactory), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IXpsOMObjectFactory1), factory.PutVoid());
    if (FAILED(hr)) return fail(hr, L"XpsOMObjectFactory");
    ComPtr<IXpsOMPackage1> package;
    hr = factory->CreatePackageFromStream1(stream.Get(), FALSE, package.Put());
    if (FAILED(hr)) return fail(hr, L"CreatePackageFromStream1");

    ComPtr<IXpsOMDocumentSequence> sequence;
    if (FAILED(hr = package->GetDocumentSequence(sequence.Put())) || !sequence) return fail(hr, L"GetDocumentSequence");
    // Orden trabajo -> documento -> página: cada nivel pisa lo que declara
    // el anterior, así el más específico prevalece.
    ComPtr<IXpsOMPrintTicketResource> jobTicket;
    sequence->GetPrintTicketResource(jobTicket.Put());
    ApplyPrintTicket(TicketXml(jobTicket.Get()), facts);

    ComPtr<IXpsOMDocumentCollection> documents;
    if (FAILED(hr = sequence->GetDocuments(documents.Put())) || !documents) return fail(hr, L"GetDocuments");
    UINT32 docCount = 0;
    documents->GetCount(&docCount);
    std::uint32_t pages = 0;
    for (UINT32 d = 0; d < docCount; ++d) {
        ComPtr<IXpsOMDocument> document;
        if (FAILED(documents->GetAt(d, document.Put())) || !document) continue;
        if (d == 0) {
            ComPtr<IXpsOMPrintTicketResource> docTicket;
            document->GetPrintTicketResource(docTicket.Put());
            ApplyPrintTicket(TicketXml(docTicket.Get()), facts);
        }
        ComPtr<IXpsOMPageReferenceCollection> references;
        if (FAILED(document->GetPageReferences(references.Put())) || !references) continue;
        UINT32 count = 0;
        references->GetCount(&count);
        if (d == 0 && count > 0) {
            // Opciones de página (papel, orientación) de la primera página.
            ComPtr<IXpsOMPageReference> first;
            if (SUCCEEDED(references->GetAt(0, first.Put())) && first) {
                ComPtr<IXpsOMPrintTicketResource> pageTicket;
                first->GetPrintTicketResource(pageTicket.Put());
                ApplyPrintTicket(TicketXml(pageTicket.Get()), facts);
            }
        }
        // Cada referencia de página es una FixedPage (no se cargan sus partes).
        pages += count;
    }
    facts.pageSource = core::ContentFacts::PageSource::XpsFixedPages;
    facts.pages = pages;
    return true;
}

}  // namespace cola::preview
