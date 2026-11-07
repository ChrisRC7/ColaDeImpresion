// Barra superior de la ventana principal: distribución de botones según el
// ancho disponible, dibujo, descripciones emergentes, EDIT de búsqueda y modo
// servidor, y clics de los botones.

#include "ui/HeaderBar.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;

namespace {
// Id de control del EDIT: identifica sus WM_COMMAND (EN_CHANGE, EN_SETFOCUS...).
constexpr int kEditId = 100;
}

bool HeaderBar::Create(HWND parent, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    // WS_CLIPCHILDREN: el pintado de la barra no tapa el EDIT hijo (si no,
    // parpadearía al repintar la barra).
    if (!CreateWin(L"ColaHeaderBar", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0,
                   parent))
        return false;
    // EDIT nativo sin borde (el marco redondeado se dibuja en Paint). Es el
    // único control nativo de la interfaz (D8): escribir, seleccionar, IME y
    // portapapeles salen gratis.
    edit_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), GetModuleHandleW(nullptr), nullptr);
    // Control de descripciones emergentes común a todos los botones.
    // TTS_ALWAYSTIP: se muestran aunque la ventana no esté activa;
    // TTS_NOPREFIX: '&' se muestra tal cual.
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    // Una "herramienta" por botón (uId = índice + 1, porque 0 no es buen id).
    // TTF_SUBCLASS: el control intercepta solo los mensajes del mouse de la
    // barra, sin tener que reenviarlos con TTM_RELAYEVENT. El rectángulo y el
    // texto se completan en UpdateTooltips.
    for (int i = 0; i < static_cast<int>(HeaderButton::Count_); ++i) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = hwnd_;
        ti.uId = static_cast<UINT_PTR>(i + 1);
        ti.lpszText = const_cast<LPWSTR>(L"");
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    OnStyleChanged();
    return true;
}

int HeaderBar::PreferredHeight() const { return Style::Get().Px(56); }

void HeaderBar::OnStyleChanged() {
    // La fuente del EDIT cambia con el DPI (la vieja se borró en Style::RebuildFonts).
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(Style::Get().fonts().normal), TRUE);
    ApplyControlDarkMode(edit_, Style::Get().dark());
    // Los textos dependen del idioma; la distribución, del DPI y del largo de los textos.
    UpdateCueBanner();
    UpdateTooltips();
    Layout();
    Invalidate();
    // El EDIT se repinta aparte (WS_CLIPCHILDREN lo excluye del pintado de la
    // barra); sus colores vienen de WM_CTLCOLOREDIT.
    InvalidateRect(edit_, nullptr, TRUE);
}

void HeaderBar::UpdateTooltips() {
    // Textos fijos en el mismo orden que HeaderButton; el static_assert avisa
    // si se agrega un botón fijo sin su texto.
    const S texts[] = {S::TipPrinter, S::TipHistory, S::TipHelp, S::TipTheme, S::TipMenu, S::TipDetails, S::TipAbout};
    static_assert(sizeof(texts) / sizeof(texts[0]) == static_cast<size_t>(HeaderButton::PauseJobs),
                  "un texto por botón fijo; los de pausa y exportar traen el suyo");
    for (int i = 0; i < static_cast<int>(HeaderButton::Count_); ++i) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = hwnd_;
        ti.uId = static_cast<UINT_PTR>(i + 1);
        // El rectángulo sigue al botón: si cambia la distribución hay que
        // avisarlo (TTM_NEWTOOLRECT) o la descripción aparecería en otro lado.
        ti.rect = buttons_[i];
        const auto button = static_cast<HeaderButton>(i);
        const bool pause = button == HeaderButton::PauseJobs || button == HeaderButton::PausePrinter;
        // Los botones de pausa y Exportar tienen texto variable (acción y
        // atajo actuales); el control copia el texto, así que el puntero
        // temporal alcanza.
        ti.lpszText = const_cast<LPWSTR>(pause                              ? PauseState(button).tooltip.c_str()
                                         : button == HeaderButton::Export ? exportTooltip_.c_str()
                                                                          : Tr(texts[i]));
        SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

void HeaderBar::UpdateCueBanner() {
    // Búsqueda normal, servidor genérico ("Conectar a servidor") o el texto
    // propio que pasó BeginServerInput (p. ej. "servidor del LAIMI").
    const wchar_t* text = !serverMode_                  ? Tr(S::SearchPlaceholder)
                          : serverPlaceholder_.empty() ? Tr(S::ServerPlaceholder)
                                                       : serverPlaceholder_.c_str();
    // wParam = TRUE: el texto de ayuda se sigue viendo con el foco en el cuadro.
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text));
}

void HeaderBar::SetPrinterLabel(const std::wstring& title, const std::wstring& subtitle, bool warning) {
    if (title == printerTitle_ && subtitle == printerSubtitle_ && warning == printerWarning_) return;
    printerTitle_ = title;
    printerSubtitle_ = subtitle;
    printerWarning_ = warning;
    // El ancho del selector depende del texto, y con él el de la búsqueda.
    Layout();
    Invalidate();
}

void HeaderBar::SetHistoryActive(bool active) {
    if (historyActive_ == active) return;
    historyActive_ = active;
    Layout();  // el botón "Exportar a Excel" solo ocupa lugar en el historial
    Invalidate();
}

void HeaderBar::SetExportTooltip(const std::wstring& text) {
    if (text == exportTooltip_) return;
    exportTooltip_ = text;
    UpdateTooltips();
}

int HeaderBar::ExportButtonWidth() const {
    const auto& style = Style::Get();
    // Fuera del historial el botón no existe (ancho 0 = no ocupa lugar).
    if (!historyActive_) return 0;
    // Sigue el mismo modo compacto que los botones de pausa.
    if (!pauseLabels_) return style.Px(36);  // solo el ícono
    HDC dc = GetDC(hwnd_);
    const LONG text = gfx::MeasureText(dc, Tr(S::BtnExport), style.fonts().normal).cx;
    ReleaseDC(hwnd_, dc);
    // Margen izquierdo 10 + ícono 20 + separación 6 + texto + margen derecho 12 (DIP).
    return style.Px(10 + 20 + 6) + static_cast<int>(text) + style.Px(12);
}

void HeaderBar::SetDetailsActive(bool active) {
    if (detailsActive_ == active) return;
    detailsActive_ = active;
    Invalidate();
}

void HeaderBar::SetPauseButtons(const PauseButtonState& jobs, const PauseButtonState& printer) {
    // MainWindow lo llama con cada cambio de selección o instantánea: solo se
    // actúa si algo cambió.
    if (jobs == pauseJobs_ && printer == pausePrinter_) return;
    pauseJobs_ = jobs;
    pausePrinter_ = printer;
    UpdateTooltips();
    // Solo se repintan los dos botones (el ancho no cambia: se calcula con el
    // texto más largo de ambos estados, así la barra no "salta").
    InvalidateRect(hwnd_, &buttons_[static_cast<int>(HeaderButton::PauseJobs)], FALSE);
    InvalidateRect(hwnd_, &buttons_[static_cast<int>(HeaderButton::PausePrinter)], FALSE);
}

const PauseButtonState& HeaderBar::PauseState(HeaderButton id) const {
    return id == HeaderButton::PauseJobs ? pauseJobs_ : pausePrinter_;
}

// El ancho se calcula con el más largo de "Pausar…" y "Reanudar…": así el
// botón no cambia de tamaño (ni mueve a los demás) al cambiar de estado.
int HeaderBar::PauseButtonWidth(HeaderButton id) const {
    const auto& style = Style::Get();
    if (!pauseLabels_) return style.Px(52);  // dos íconos
    const bool jobs = id == HeaderButton::PauseJobs;
    HDC dc = GetDC(hwnd_);
    HFONT font = style.fonts().normal;
    const LONG text = std::max(gfx::MeasureText(dc, Tr(jobs ? S::BtnPauseJobs : S::BtnPausePrinter), font).cx,
                               gfx::MeasureText(dc, Tr(jobs ? S::BtnResumeJobs : S::BtnResumePrinter), font).cx);
    ReleaseDC(hwnd_, dc);
    return style.Px(10 + 20 + 6) + static_cast<int>(text) + style.Px(12);  // margen, ícono, separación, texto
}

void HeaderBar::FocusSearch() {
    SetFocus(edit_);
    // Seleccionar todo: escribir reemplaza la búsqueda anterior.
    SendMessageW(edit_, EM_SETSEL, 0, -1);
}

std::wstring HeaderBar::EditText() const {
    int len = GetWindowTextLengthW(edit_);
    // +1 para el terminador que escribe GetWindowTextW; luego se recorta.
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(edit_, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

void HeaderBar::ClearSearch() {
    // En modo servidor el texto es el nombre que se está escribiendo, no una búsqueda.
    if (serverMode_) return;
    // SetWindowTextW genera EN_CHANGE, que avisa searchChanged("").
    SetWindowTextW(edit_, L"");
}

void HeaderBar::BeginServerInput(const std::wstring& current, const std::wstring& placeholder) {
    // Se guarda la búsqueda solo al entrar (si ya estaba en modo servidor, el
    // texto actual es un servidor y no debe pisar la búsqueda guardada).
    if (!serverMode_) savedSearch_ = EditText();
    serverMode_ = true;
    serverPlaceholder_ = placeholder;
    // Cambio por código: no debe filtrar la lista como si fuera una búsqueda.
    suppressChange_ = true;
    SetWindowTextW(edit_, current.c_str());
    suppressChange_ = false;
    UpdateCueBanner();
    FocusSearch();
    // Repinta para cambiar la lupa por el ícono de servidor.
    Invalidate();
}

void HeaderBar::EndServerInput() {
    if (!serverMode_) return;
    serverMode_ = false;
    // Se restaura la búsqueda sin avisar: el filtro de la lista no cambió.
    suppressChange_ = true;
    SetWindowTextW(edit_, savedSearch_.c_str());
    suppressChange_ = false;
    UpdateCueBanner();
    Invalidate();
}

RECT HeaderBar::ButtonScreenRect(HeaderButton button) const {
    RECT r = buttons_[static_cast<int>(button)];
    // Un RECT son dos POINT seguidos: MapWindowPoints convierte ambas esquinas
    // de coordenadas cliente a pantalla (hWndTo = nullptr = pantalla).
    MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

void HeaderBar::Layout() {
    // Puede llamarse antes de crear la ventana (desde los Set* iniciales).
    if (!hwnd_) return;
    const auto& style = Style::Get();
    RECT client;
    GetClientRect(hwnd_, &client);
    const int pad = style.Px(12);   // margen a los lados
    const int btn = style.Px(36);   // botón de ícono cuadrado
    const int cy = (client.top + client.bottom) / 2;  // todo se centra verticalmente

    // Botones a la derecha (de derecha a izquierda: menú, acerca de, tema, ayuda,
    // historial, detalles).
    // Tienen ancho fijo y siempre están; el resto se acomoda en lo que sobra.
    int x = client.right - pad;
    for (HeaderButton b : {HeaderButton::Menu, HeaderButton::About, HeaderButton::Theme, HeaderButton::Help, HeaderButton::History,
                           HeaderButton::Details}) {
        buttons_[static_cast<int>(b)] = {x - btn, cy - btn / 2, x, cy + btn / 2};
        x -= btn + style.Px(2);
    }
    // Hasta dónde puede llegar lo de la izquierda (10 DIP antes de Detalles).
    const int rightLimit = x - style.Px(10);
    const int gap = style.Px(6);
    // Botones de pausa a la derecha de la búsqueda; en el historial se suma el
    // botón "Exportar a Excel" a la derecha de los de pausa.
    // (Lambda porque el resultado depende de pauseLabels_, que se prueba con
    // ambos valores más abajo.)
    auto buttonsWidth = [&] {
        const int exportW = ExportButtonWidth();
        return PauseButtonWidth(HeaderButton::PauseJobs) + gap + PauseButtonWidth(HeaderButton::PausePrinter) +
               (exportW ? gap + exportW : 0);
    };
    pauseLabels_ = false;
    const int compactW = buttonsWidth();  // solo con íconos: lo mínimo que ocupan

    // Selector de impresora a la izquierda (ancho según el texto). Con la ventana
    // angosta se achica (el texto se recorta con "…") para que los botones de
    // pausa y exportar no se monten sobre los de la derecha.
    HDC dc = GetDC(hwnd_);
    SIZE titleSize = gfx::MeasureText(dc, printerTitle_, style.fonts().semibold);
    SIZE subSize = gfx::MeasureText(dc, printerSubtitle_, style.fonts().small);
    ReleaseDC(hwnd_, dc);
    // Ancho natural: margen 12 + ícono 24 + separación 8 + texto más largo +
    // 28 para la flecha ▾ (DIP).
    int printerW = style.Px(12 + 24 + 8) + static_cast<int>(std::max(titleSize.cx, subSize.cx)) + style.Px(28);
    // Espacio que queda si los botones de pausa van compactos y sin búsqueda.
    const int room = rightLimit - client.left - pad - style.Px(12) - compactW;
    // Máximo: la mitad del ancho útil (al menos 160 DIP), sin pasar de `room`.
    const int maxW = std::min(std::max(style.Px(160), (rightLimit - pad) / 2), room);
    const int minW = std::min(style.Px(160), maxW);
    // Nunca menos de 96 DIP aunque no quepa (ventana muy angosta: mejor que
    // algo se superponga a que el selector desaparezca).
    printerW = std::clamp(printerW, std::max(minW, style.Px(96)), std::max(maxW, style.Px(96)));
    buttons_[static_cast<int>(HeaderButton::Printer)] = {client.left + pad, cy - style.Px(20),
                                                         client.left + pad + printerW, cy + style.Px(20)};

    // Búsqueda: el espacio restante (máx. 480 dip), con los botones de pausa a
    // su derecha. Si no alcanza, los botones quedan solo con el ícono.
    // Primero se prueba con textos; si a la búsqueda le quedarían menos de
    // 200 DIP, se pasa al modo compacto.
    int left = buttons_[static_cast<int>(HeaderButton::Printer)].right + style.Px(12);
    pauseLabels_ = true;
    int pauseW = buttonsWidth();
    if (rightLimit - left - pauseW - gap < style.Px(200)) {
        pauseLabels_ = false;
        pauseW = buttonsWidth();
    }
    // Con menos de 80 DIP la búsqueda no sirve: se oculta (ancho 0) y los
    // botones de pausa ocupan su lugar.
    int width = std::min(rightLimit - left - pauseW - style.Px(8), style.Px(480));
    if (width < style.Px(80)) width = 0;
    searchBox_ = {left, cy - style.Px(17), left + width, cy + style.Px(17)};
    // Botones de pausa (y Exportar) justo a la derecha de la búsqueda (34 DIP de alto).
    int px = width > 0 ? static_cast<int>(searchBox_.right) + style.Px(8) : left;
    for (HeaderButton b : {HeaderButton::PauseJobs, HeaderButton::PausePrinter}) {
        const int w = PauseButtonWidth(b);
        buttons_[static_cast<int>(b)] = {px, cy - style.Px(17), px + w, cy + style.Px(17)};
        px += w + gap;
    }
    const int exportW = ExportButtonWidth();
    // Fuera del historial queda un RECT vacío (no se dibuja ni recibe clics).
    buttons_[static_cast<int>(HeaderButton::Export)] =
        exportW ? RECT{px, cy - style.Px(17), px + exportW, cy + style.Px(17)} : RECT{};

    // El EDIT ocupa el interior del marco: deja 34 DIP a la izquierda para la
    // lupa y 10 a la derecha, con el alto justo de una línea de texto ("Ag"
    // cubre ascendentes y descendentes) centrado verticalmente.
    HFONT font = style.fonts().normal;
    dc = GetDC(hwnd_);
    SIZE lineSize = gfx::MeasureText(dc, L"Ag", font);
    ReleaseDC(hwnd_, dc);
    int editLeft = searchBox_.left + style.Px(34);
    int editH = lineSize.cy + style.Px(2);
    MoveWindow(edit_, editLeft, cy - editH / 2, std::max(0, static_cast<int>(searchBox_.right) - style.Px(10) - editLeft), editH, TRUE);
    ShowWindow(edit_, width > 0 ? SW_SHOW : SW_HIDE);
    // Los rectángulos de las descripciones siguen a los botones.
    UpdateTooltips();
}

void HeaderBar::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    // Fondo y línea inferior que separa la barra de la lista.
    gfx::Fill(dc, client, pal.surfaceAlt);
    gfx::HLine(dc, client.left, client.right, client.bottom - 1, pal.border);

    // Selector de impresora.
    // Tarjeta redondeada con: ícono (impresora, o advertencia si hay
    // problemas), título y subtítulo, y una flecha ▾ que indica que abre un menú.
    RECT pr = buttons_[static_cast<int>(HeaderButton::Printer)];
    int printerIndex = static_cast<int>(HeaderButton::Printer);
    gfx::FillRounded(dc, pr, style.Px(8), hover_ == printerIndex ? pal.hover : pal.surface);
    gfx::StrokeRounded(dc, pr, style.Px(8), pal.border);
    RECT icon{pr.left + style.Px(8), pr.top, pr.left + style.Px(34), pr.bottom};
    gfx::Glyph(dc, printerWarning_ ? glyph::Warning : glyph::Printer, icon, fonts.icon,
               printerWarning_ ? pal.warning : pal.accent);
    RECT chevron{pr.right - style.Px(26), pr.top, pr.right - style.Px(6), pr.bottom};
    gfx::Glyph(dc, glyph::ChevronDown, chevron, fonts.iconSmall, pal.textMuted);
    // Con subtítulo: título en la mitad superior (pegado abajo) y subtítulo en
    // la inferior (pegado arriba), así quedan juntos en el centro.
    RECT titleRect{icon.right + style.Px(4), pr.top + style.Px(3), chevron.left, (pr.top + pr.bottom) / 2 + style.Px(2)};
    RECT subRect{icon.right + style.Px(4), (pr.top + pr.bottom) / 2, chevron.left, pr.bottom - style.Px(3)};
    if (printerSubtitle_.empty()) {
        // Sin subtítulo: el título centrado verticalmente en toda la tarjeta.
        gfx::Text(dc, printerTitle_, RECT{titleRect.left, pr.top, titleRect.right, pr.bottom}, fonts.semibold, pal.text);
    } else {
        gfx::Text(dc, printerTitle_, titleRect, fonts.semibold, pal.text,
                  DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        gfx::Text(dc, printerSubtitle_, subRect, fonts.small, printerWarning_ ? pal.warning : pal.textMuted,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // Cuadro de búsqueda / servidor.
    // Marco redondeado detrás del EDIT nativo; con el foco el borde pasa a
    // color de acento y más grueso (indicador de foco visible).
    if (gfx::Width(searchBox_) > 0) {
        bool focused = GetFocus() == edit_;
        gfx::FillRounded(dc, searchBox_, style.Px(8), pal.surface);
        gfx::StrokeRounded(dc, searchBox_, style.Px(8), focused ? pal.accent : pal.border, focused ? 1.5f : 1.0f);
        RECT glyphRect{searchBox_.left + style.Px(6), searchBox_.top, searchBox_.left + style.Px(32), searchBox_.bottom};
        // Lupa al buscar; ícono de servidor en modo servidor.
        gfx::Glyph(dc, serverMode_ ? glyph::Server : glyph::Search, glyphRect, fonts.iconSmall, pal.textMuted);
    }

    PaintPauseButton(dc, HeaderButton::PauseJobs, pauseJobs_);
    PaintPauseButton(dc, HeaderButton::PausePrinter, pausePrinter_);
    PaintExportButton(dc);

    // Botones de ícono.
    // `active` = estado "encendido" (fondo de selección e ícono de acento),
    // usado por Detalles e Historial, que son conmutadores.
    struct IconButton {
        HeaderButton id;
        wchar_t glyph;
        bool active;
    };
    // El ícono de Tema muestra el modo al que se cambiará (sol en oscuro,
    // luna en claro); el de Detalles, si abre o cierra el panel.
    const IconButton icons[] = {
        {HeaderButton::Details, detailsActive_ ? glyph::ClosePane : glyph::OpenPane, detailsActive_},
        {HeaderButton::History, glyph::History, historyActive_},
        {HeaderButton::Help, glyph::Keyboard, false},
        {HeaderButton::Theme, style.dark() ? glyph::Sun : glyph::Moon, false},
        {HeaderButton::About, glyph::About, false},
        {HeaderButton::Menu, glyph::More, false},
    };
    for (const auto& b : icons) {
        RECT r = buttons_[static_cast<int>(b.id)];
        int index = static_cast<int>(b.id);
        if (b.active) gfx::FillRounded(dc, r, style.Px(8), pal.selection);
        else if (hover_ == index) gfx::FillRounded(dc, r, style.Px(8), pal.hover);
        gfx::Glyph(dc, b.glyph, r, fonts.icon, b.active ? pal.accent : pal.text);
    }
}

void HeaderBar::PaintPauseButton(HDC dc, HeaderButton id, const PauseButtonState& state) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    const RECT r = buttons_[static_cast<int>(id)];
    if (gfx::Width(r) <= 0) return;
    // Deshabilitado: sin resaltado al pasar el mouse y colores apagados.
    const bool hovered = state.enabled && hover_ == static_cast<int>(id);
    gfx::FillRounded(dc, r, style.Px(8), hovered ? pal.hover : pal.surface);
    gfx::StrokeRounded(dc, r, style.Px(8), pal.border);
    // "Reanudar" se destaca con el color de acento: es lo que hace falta tras la retención.
    // Gris de deshabilitado: el texto atenuado mezclado casi a la mitad con el fondo.
    const COLORREF disabled = Blend(pal.textMuted, pal.surfaceAlt, 0.45);
    const COLORREF glyphColor = !state.enabled ? disabled : state.resume ? pal.accent : pal.text;
    const COLORREF textColor = !state.enabled ? disabled : pal.text;
    // El ícono muestra la acción que se hará (D37): ▶ reanudar, ⏸ pausar.
    const wchar_t g = state.resume ? glyph::Play : glyph::Pause;
    const bool jobs = id == HeaderButton::PauseJobs;
    if (!pauseLabels_) {
        // Solo íconos: documento o impresora + pausa/reanudar, para distinguir los dos botones.
        const int mid = (r.left + r.right) / 2;
        gfx::Glyph(dc, jobs ? glyph::Document : glyph::Printer, RECT{r.left + style.Px(4), r.top, mid, r.bottom},
                   fonts.iconSmall, textColor);
        gfx::Glyph(dc, g, RECT{mid, r.top, r.right - style.Px(4), r.bottom}, fonts.iconSmall, glyphColor);
        return;
    }
    // Con texto: ícono de 20 DIP a 10 del borde y la etiqueta de la acción.
    RECT icon{r.left + style.Px(10), r.top, r.left + style.Px(30), r.bottom};
    gfx::Glyph(dc, g, icon, fonts.iconSmall, glyphColor);
    const S label = jobs ? (state.resume ? S::BtnResumeJobs : S::BtnPauseJobs)
                         : (state.resume ? S::BtnResumePrinter : S::BtnPausePrinter);
    RECT text{icon.right + style.Px(6), r.top, r.right - style.Px(8), r.bottom};
    gfx::Text(dc, Tr(label), text, fonts.normal, textColor,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void HeaderBar::PaintExportButton(HDC dc) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    const RECT r = buttons_[static_cast<int>(HeaderButton::Export)];
    // Ancho 0 = no estamos en el historial.
    if (gfx::Width(r) <= 0) return;
    gfx::FillRounded(dc, r, style.Px(8), hover_ == static_cast<int>(HeaderButton::Export) ? pal.hover : pal.surface);
    gfx::StrokeRounded(dc, r, style.Px(8), pal.border);
    // Modo compacto: solo el ícono, centrado.
    if (!pauseLabels_) {
        gfx::Glyph(dc, glyph::Export, r, fonts.iconSmall, pal.accent);
        return;
    }
    RECT icon{r.left + style.Px(10), r.top, r.left + style.Px(30), r.bottom};
    gfx::Glyph(dc, glyph::Export, icon, fonts.iconSmall, pal.accent);
    RECT text{icon.right + style.Px(6), r.top, r.right - style.Px(8), r.bottom};
    gfx::Text(dc, Tr(S::BtnExport), text, fonts.normal, pal.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

int HeaderBar::HitTest(POINT pt) const {
    // Los rectángulos no se superponen; los vacíos (Exportar fuera del
    // historial) nunca contienen el punto.
    for (int i = 0; i < static_cast<int>(HeaderButton::Count_); ++i)
        if (gfx::Contains(buttons_[i], pt)) return i;
    return -1;
}

LRESULT HeaderBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            // Doble búfer (el EDIT hijo se pinta solo, excluido por WS_CLIPCHILDREN).
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            // Sin CS_HREDRAW solo se pintaría la franja nueva y quedarían restos
            // de la distribución anterior mientras se arrastra el borde.
            Layout();
            Invalidate();
            return 0;
        case WM_CTLCOLOREDIT: {
            // El EDIT pide sus colores al padre antes de pintarse: se le dan los
            // de la paleta (así sigue el modo oscuro) y el pincel de su fondo.
            const auto& pal = Style::Get().palette();
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, pal.text);
            SetBkColor(dc, pal.surface);
            return reinterpret_cast<LRESULT>(Style::Get().Brush(pal.surface));
        }
        case WM_COMMAND:
            // Notificaciones del EDIT.
            if (LOWORD(wParam) == kEditId) {
                // Cada cambio de texto filtra la lista en vivo, salvo en modo
                // servidor (el texto es un nombre de servidor) o si lo cambió el código.
                if (HIWORD(wParam) == EN_CHANGE && !serverMode_ && !suppressChange_ && callbacks_.searchChanged)
                    callbacks_.searchChanged(EditText());
                // Al ganar o perder el foco se repinta el marco (borde de acento).
                if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS) InvalidateRect(hwnd_, &searchBox_, FALSE);
                return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            // Pide WM_MOUSELEAVE una vez por entrada para quitar el resaltado al salir.
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hit != hover_) {
                hover_ = hit;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hover_ = -1;
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            // Clic en el marco (la lupa o los márgenes, fuera del EDIT): da el
            // foco al cuadro, como si se hubiera hecho clic en él.
            if (gfx::Contains(searchBox_, pt)) {
                SetFocus(edit_);
                return 0;
            }
            // Los botones actúan al soltar (como los nativos): aquí solo se recuerda dónde se presionó.
            pressed_ = HitTest(pt);
            return 0;
        }
        case WM_LBUTTONUP: {
            // Sin SetCapture: si se suelta fuera de la barra este mensaje no
            // llega y pressed_ conserva el valor viejo; el próximo
            // WM_LBUTTONDOWN sobre la barra lo reemplaza.
            int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hit >= 0) {
                const auto button = static_cast<HeaderButton>(hit);
                if ((button == HeaderButton::PauseJobs || button == HeaderButton::PausePrinter) &&
                    !PauseState(button).enabled)
                    hit = -1;  // deshabilitado
            }
            // Clic válido: se presionó y se soltó sobre el mismo botón.
            if (hit >= 0 && hit == pressed_ && callbacks_.clicked)
                callbacks_.clicked(static_cast<HeaderButton>(hit), ButtonScreenRect(static_cast<HeaderButton>(hit)));
            pressed_ = -1;
            return 0;
        }
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
