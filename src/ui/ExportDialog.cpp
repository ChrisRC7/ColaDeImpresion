// Diálogo "Exportar historial a Excel" (ver ExportDialog.h). Es una ventana
// propia (no un recurso DIALOG de Win32): se pinta a mano con la paleta de la
// app para respetar el modo oscuro (D8) y corre su propio bucle de mensajes
// modal. Contiene además la lógica pura de la elección (NormalizeExportChoice,
// ExportChoiceRecords), que se prueba en tests/test_history_export.cpp.
#include "ui/ExportDialog.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/DarkMode.h"
#include "ui/Gfx.h"
#include "ui/Style.h"
#include "ui/Window.h"
#include "util/Strings.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;

void NormalizeExportChoice(ExportChoice& choice, size_t dayCount, bool fromChanged) {
    // Sin días con registros no hay rango posible: se vuelve a "todo" con
    // índices en cero (el diálogo igual muestra "—" en los campos).
    if (dayCount == 0) {
        choice = {};
        return;
    }
    // Los índices los arma el llamador o el propio diálogo; por seguridad se
    // acotan al último día disponible para no indexar fuera de days.
    choice.from = std::min(choice.from, dayCount - 1);
    choice.to = std::min(choice.to, dayCount - 1);
    // El extremo que se acaba de cambiar manda: arrastra al otro.
    if (choice.from > choice.to) {
        if (fromChanged) choice.to = choice.from;
        else choice.from = choice.to;
    }
}

std::uint32_t ExportChoiceRecords(const std::vector<ExportDay>& days, const ExportChoice& choice) {
    // Suma de los registros de cada día incluido: todos si choice.all, si no
    // solo los del rango cerrado [from, to].
    std::uint32_t total = 0;
    for (size_t i = 0; i < days.size(); ++i)
        if (choice.all || (i >= choice.from && i <= choice.to)) total += days[i].records;
    return total;
}

namespace {

// Elementos interactivos del diálogo, en el orden en que los recorre Tab.
// Sirven de índice en rects_ y como valor de focus_/hover_ (-1 = ninguno).
// ItemCount no es un elemento: es la cantidad.
enum Item { OptAll, OptRange, From, To, Cancel, Export, ItemCount };

// Ventana del diálogo. Vive solo durante ShowExportDialog: Run() la crea,
// atiende mensajes hasta que el usuario decide y la destruye.
class ExportDialog : public Window {
public:
    // days se guarda por referencia (el vector del llamador sobrevive al
    // diálogo); choice es la elección inicial, ya normalizada.
    ExportDialog(const std::vector<ExportDay>& days, ExportChoice choice) : days_(days), choice_(choice) {}

    // Muestra el diálogo centrado sobre owner y bloquea hasta que se cierre.
    // Devuelve true si el usuario eligió "Exportar".
    bool Run(HWND owner) {
        const Style& style = Style::Get();
        // El tamaño del área cliente sale del layout (ancho fijo de 480 px
        // lógicos, alto según los elementos); AdjustWindowRectEx le suma la
        // barra de título y los bordes para obtener el tamaño de la ventana.
        const RECT client{0, 0, style.Px(480), Layout()};
        RECT frame = client;
        const DWORD ws = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        const DWORD ex = WS_EX_DLGMODALFRAME;
        AdjustWindowRectEx(&frame, ws, FALSE, ex);
        const int w = frame.right - frame.left, h = frame.bottom - frame.top;
        // Centrado horizontal sobre la ventana principal y a un tercio de su
        // alto (como los diálogos de Windows); nunca por encima de su borde.
        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int x = ownerRect.left + (ownerRect.right - ownerRect.left - w) / 2;
        const int y = ownerRect.top + std::max(0L, (ownerRect.bottom - ownerRect.top - h) / 3);
        if (!CreateWin(L"ColaExportDialog", Tr(S::ExportDialogTitle), ws, ex, x, y, w, h, owner)) return false;
        // Barra de título oscura si la app está en modo oscuro.
        ApplyWindowDarkMode(hwnd_, style.dark());

        // Modalidad a mano: se deshabilita el dueño (no recibe clics ni
        // teclado) y se corre un bucle de mensajes local hasta que Close()
        // marque done_.
        EnableWindow(owner, FALSE);
        ShowWindow(hwnd_, SW_SHOW);
        SetFocus(hwnd_);
        MSG msg;
        bool quit = false;
        while (!done_) {
            // WM_QUIT (o error) mientras el diálogo está abierto: se sale del
            // bucle y se vuelve a publicar más abajo para que el bucle
            // principal de la app también termine.
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Reactivar el dueño antes de destruir: si no, Windows activa otra aplicación.
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
        DestroyWindow(hwnd_);
        if (quit) PostQuitMessage(static_cast<int>(msg.wParam));
        return result_;
    }

    // Elección final (válida tras Run(); se copia al llamador si exportó).
    const ExportChoice& choice() const { return choice_; }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
            case WM_PAINT: Paint(); return 0;
            // Todo el fondo se pinta en Paint() con doble búfer: borrar antes
            // solo produciría parpadeo.
            case WM_ERASEBKGND: return 1;
            case WM_MOUSEMOVE: {
                // Resaltado al pasar el mouse: solo se repinta si cambió el
                // elemento bajo el cursor.
                const int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (hit != hover_) {
                    hover_ = hit;
                    Invalidate();
                }
                // Pide WM_MOUSELEAVE para quitar el resaltado al salir.
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                return 0;
            }
            case WM_MOUSELEAVE:
                hover_ = -1;
                Invalidate();
                return 0;
            case WM_LBUTTONDOWN: {
                // El clic enfoca el elemento y lo activa (selecciona la
                // opción, abre la lista de días o pulsa el botón).
                const int hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (hit >= 0) {
                    focus_ = hit;
                    Activate(hit);
                }
                return 0;
            }
            case WM_KEYDOWN: OnKey(static_cast<UINT>(wParam)); return 0;
            case WM_SYSKEYDOWN:
                // Alt+↓ abre la lista de días (como un cuadro combinado).
                if (wParam == VK_DOWN && (focus_ == From || focus_ == To)) {
                    Activate(focus_);
                    return 0;
                }
                break;
            // La X de la barra de título equivale a Cancelar.
            case WM_CLOSE: Close(false); return 0;
            default: break;
        }
        return DefaultProc(msg, wParam, lParam);
    }

private:
    // Calcula los rectángulos y devuelve el alto del área cliente.
    // Disposición, de arriba abajo: la pregunta, la opción "todo" (dos líneas:
    // título y detalle), la opción "rango", la fila "Desde [..] Hasta [..]"
    // sangrada bajo la opción de rango, la línea con el conteo de registros
    // y los botones alineados a la derecha. Todo en píxeles lógicos
    // escalados por DPI con Style::Px.
    int Layout() {
        const Style& style = Style::Get();
        const int pad = style.Px(20);
        const int right = style.Px(480) - pad;
        int y = pad;
        prompt_ = {pad, y, right, y + style.Px(22)};
        y = prompt_.bottom + style.Px(10);
        rects_[OptAll] = {pad, y, right, y + style.Px(50)};
        y = rects_[OptAll].bottom + style.Px(4);
        rects_[OptRange] = {pad, y, right, y + style.Px(36)};
        y = rects_[OptRange].bottom + style.Px(6);

        // Fila de fechas: etiqueta + campo, dos veces, alineada con el texto
        // de la opción de rango (sangría de 30 px).
        const int indent = pad + style.Px(30);
        const int label = style.Px(48), field = style.Px(150), gap = style.Px(14);
        fromLabel_ = {indent, y, indent + label, y + style.Px(32)};
        rects_[From] = {fromLabel_.right, y, fromLabel_.right + field, fromLabel_.bottom};
        toLabel_ = {rects_[From].right + gap, y, rects_[From].right + gap + label, fromLabel_.bottom};
        rects_[To] = {toLabel_.right, y, toLabel_.right + field, fromLabel_.bottom};
        y = rects_[To].bottom + style.Px(8);
        count_ = {indent, y, right, y + style.Px(20)};
        y = count_.bottom + style.Px(20);

        // Botones: "Exportar" pegado al margen derecho y "Cancelar" a su
        // izquierda, separados 8 px.
        const int buttonH = style.Px(34);
        rects_[Export] = {right - style.Px(110), y, right, y + buttonH};
        rects_[Cancel] = {rects_[Export].left - style.Px(8) - style.Px(100), y, rects_[Export].left - style.Px(8),
                          y + buttonH};
        return y + buttonH + pad;
    }

    // Elemento interactivo bajo el punto p (coordenadas cliente), o -1.
    int HitTest(POINT p) const {
        for (int i = 0; i < ItemCount; ++i)
            if (gfx::Contains(rects_[i], p)) return i;
        return -1;
    }

    // Termina el bucle modal de Run() con el resultado dado (true = exportar).
    void Close(bool result) {
        result_ = result;
        done_ = true;
    }

    // Acción de un elemento (clic, Espacio o F4/Alt+↓ según el caso).
    void Activate(int item) {
        switch (item) {
            case OptAll: choice_.all = true; break;
            case OptRange: choice_.all = false; break;
            case From:
            case To:
                // Tocar un campo de fecha implica querer un rango. Se repinta
                // antes de abrir el menú para que la opción "rango" ya se vea
                // seleccionada mientras el menú está abierto (TrackPopupMenu
                // bloquea hasta que se elige).
                choice_.all = false;
                Invalidate();
                ChooseDay(item == From);
                break;
            case Cancel: Close(false); return;
            case Export: Close(true); return;
            default: break;
        }
        Invalidate();
    }

    // Mueve "Desde" (from = true) o "Hasta" un día hacia adelante (delta > 0)
    // o hacia atrás, sin salirse de la lista; luego normaliza para que el
    // otro extremo acompañe si se cruzaron.
    void StepDay(bool from, int delta) {
        choice_.all = false;
        size_t& index = from ? choice_.from : choice_.to;
        if (delta < 0 && index > 0) --index;
        if (delta > 0 && index + 1 < days_.size()) ++index;
        NormalizeExportChoice(choice_, days_.size(), from);
        Invalidate();
    }

    // Lista de días con registros, del más reciente al más antiguo.
    // Se muestra como menú emergente bajo el campo, con una marca en el día
    // elegido. El id de cada ítem es índice + 1 porque TrackPopupMenu con
    // TPM_RETURNCMD devuelve 0 cuando se cierra sin elegir.
    void ChooseDay(bool from) {
        HMENU menu = CreatePopupMenu();
        const size_t current = from ? choice_.from : choice_.to;
        // days_ va del más antiguo al más reciente; se recorre al revés para
        // que lo más reciente (lo que más se exporta) quede arriba.
        for (size_t i = days_.size(); i-- > 0;) {
            const std::wstring text = util::Format(Tr(S::ExportDayItem), days_[i].label.c_str(), days_[i].records);
            AppendMenuW(menu, MF_STRING | (i == current ? MF_CHECKED : 0), static_cast<UINT_PTR>(i + 1), text.c_str());
        }
        // Posición: esquina inferior izquierda del campo, en coordenadas de pantalla.
        const RECT& r = rects_[from ? From : To];
        POINT pt{r.left, r.bottom + Style::Get().Px(2)};
        ClientToScreen(hwnd_, &pt);
        const UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        if (id == 0) return;
        (from ? choice_.from : choice_.to) = id - 1;
        NormalizeExportChoice(choice_, days_.size(), from);
        Invalidate();
    }

    // Teclado del diálogo (ver la lista de teclas en ExportDialog.h).
    void OnKey(UINT key) {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        switch (key) {
            case VK_ESCAPE: Close(false); return;
            // Enter exporta desde cualquier elemento, salvo que el foco esté
            // en "Cancelar" (entonces cancela, como un botón normal).
            case VK_RETURN: Close(focus_ != Cancel); return;
            // Tab avanza y Mayús+Tab retrocede, dando la vuelta (sumar
            // ItemCount - 1 equivale a restar 1 en aritmética modular).
            case VK_TAB: focus_ = (focus_ + (shift ? ItemCount - 1 : 1)) % ItemCount; break;
            case VK_SPACE: Activate(focus_); return;
            // F4 abre la lista de días, como en un cuadro combinado de Windows.
            case VK_F4:
                if (focus_ == From || focus_ == To) Activate(focus_);
                return;
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT: {
                // Las flechas actúan según el grupo enfocado: en las opciones
                // eligen "todo"/"rango" (como botones de radio), en un campo
                // de fecha cambian el día y en los botones mueven el foco
                // entre ellos.
                const bool forward = key == VK_DOWN || key == VK_RIGHT;
                if (focus_ == OptAll || focus_ == OptRange) {
                    focus_ = forward ? OptRange : OptAll;
                    choice_.all = focus_ == OptAll;
                } else if (focus_ == From || focus_ == To) {
                    StepDay(focus_ == From, forward ? 1 : -1);
                } else if (focus_ == Cancel || focus_ == Export) {
                    focus_ = forward ? Export : Cancel;
                }
                break;
            }
            default: return;
        }
        Invalidate();
    }

    // Pinta todo el diálogo con doble búfer (BufferedPaint) usando la paleta
    // y las fuentes actuales de Style (claro u oscuro).
    void Paint() {
        gfx::BufferedPaint paint(hwnd_);
        HDC dc = paint.dc();
        const Style& style = Style::Get();
        const Palette& pal = style.palette();
        const Fonts& fonts = style.fonts();
        const int radius = style.Px(8);
        gfx::Fill(dc, paint.rect(), pal.surface);

        gfx::Text(dc, Tr(S::ExportDialogPrompt), prompt_, fonts.semibold, pal.text);

        // Opciones.
        // Cada opción es un botón de radio dibujado: aro (acentuado si está
        // elegida) con un punto relleno, el título y, si hay, una segunda
        // línea de detalle en letra pequeña. Con detalle, el aro se alinea con
        // la primera línea; sin detalle, se centra verticalmente.
        auto option = [&](int item, bool selected, const std::wstring& title, const std::wstring& detail) {
            const RECT& r = rects_[item];
            if (hover_ == item) gfx::FillRounded(dc, r, radius, pal.hover);
            if (focus_ == item) gfx::StrokeRounded(dc, r, radius, pal.accent, 1.5f);
            const int cx = r.left + style.Px(18), cy = detail.empty() ? (r.top + r.bottom) / 2 : r.top + style.Px(17);
            const int outer = style.Px(8);
            const RECT ring{cx - outer, cy - outer, cx + outer, cy + outer};
            gfx::StrokeRounded(dc, ring, outer, selected ? pal.accent : pal.textMuted, 1.5f);
            if (selected) gfx::FillCircle(dc, cx, cy, style.Px(4), pal.accent);
            const int textLeft = cx + outer + style.Px(12);
            if (detail.empty()) {
                gfx::Text(dc, title, {textLeft, r.top, r.right - style.Px(8), r.bottom}, fonts.normal, pal.text);
            } else {
                gfx::Text(dc, title, {textLeft, r.top + style.Px(6), r.right - style.Px(8), r.top + style.Px(28)},
                          fonts.normal, pal.text);
                gfx::Text(dc, detail, {textLeft, r.top + style.Px(26), r.right - style.Px(8), r.bottom - style.Px(4)},
                          fonts.small, pal.textMuted);
            }
        };
        // Detalle de "todo": total de registros y el primer y último día
        // ("N registros, del dd/mm/aaaa al dd/mm/aaaa").
        std::wstring allDetail;
        if (!days_.empty())
            allDetail = util::Format(Tr(S::ExportAllDetail), ExportChoiceRecords(days_, ExportChoice{}),
                                     days_.front().label.c_str(), days_.back().label.c_str());
        option(OptAll, choice_.all, Tr(S::ExportAll), allDetail);
        option(OptRange, !choice_.all, Tr(S::ExportRangeOption), L"");

        // Campos de fecha (atenuados mientras está elegido "todo").
        const COLORREF fieldText = choice_.all ? pal.textMuted : pal.text;
        gfx::Text(dc, Tr(S::ExportFrom), fromLabel_, fonts.normal, fieldText);
        gfx::Text(dc, Tr(S::ExportTo), toLabel_, fonts.normal, fieldText);
        // Cada campo imita un cuadro combinado: fondo, borde (acentuado y más
        // grueso con foco), el día elegido y una flecha ▾ a la derecha.
        auto field = [&](int item, size_t index) {
            const RECT& r = rects_[item];
            gfx::FillRounded(dc, r, radius, hover_ == item ? pal.hover : pal.surfaceAlt);
            const bool focused = focus_ == item;
            gfx::StrokeRounded(dc, r, radius, focused ? pal.accent : pal.border, focused ? 1.5f : 1.0f);
            const std::wstring text = index < days_.size() ? days_[index].label : L"—";
            gfx::Text(dc, text, {r.left + style.Px(10), r.top, r.right - style.Px(28), r.bottom}, fonts.normal,
                      fieldText);
            gfx::Glyph(dc, glyph::ChevronDown, {r.right - style.Px(28), r.top, r.right - style.Px(6), r.bottom},
                       fonts.iconSmall, pal.textMuted);
        };
        field(From, choice_.from);
        field(To, choice_.to);
        // Línea bajo las fechas: con "todo" una pista de cómo elegir un
        // rango; con rango, cuántos registros se exportarían.
        const std::wstring count = choice_.all ? std::wstring(Tr(S::ExportRangeHint))
                                               : util::Format(Tr(S::ExportRangeCount),
                                                              ExportChoiceRecords(days_, choice_));
        gfx::Text(dc, count, count_, fonts.small, pal.textMuted);

        // Botones.
        // El principal ("Exportar") va relleno con el color de acento; el
        // secundario, con fondo neutro y borde. El foco se marca con un anillo
        // 3 px por fuera del botón.
        auto button = [&](int item, const std::wstring& text, bool primary) {
            const RECT& r = rects_[item];
            COLORREF fill = primary ? pal.accent : (hover_ == item ? pal.hover : pal.surfaceAlt);
            gfx::FillRounded(dc, r, radius, fill);
            if (!primary) gfx::StrokeRounded(dc, r, radius, pal.border);
            if (focus_ == item) {
                const RECT ring = gfx::Inset(r, -style.Px(3), -style.Px(3));
                gfx::StrokeRounded(dc, ring, radius + style.Px(3), pal.accent, 1.5f);
            }
            gfx::Text(dc, text, r, primary ? fonts.semibold : fonts.normal, primary ? pal.accentText : pal.text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };
        button(Cancel, Tr(S::ExportCancel), false);
        button(Export, Tr(S::ExportButton), true);
    }

    const std::vector<ExportDay>& days_;  // días con registros, del más antiguo al más reciente
    ExportChoice choice_;                 // elección en curso (se edita en el diálogo)
    RECT rects_[ItemCount]{};             // rectángulo de cada elemento interactivo (ver Item)
    RECT prompt_{}, fromLabel_{}, toLabel_{}, count_{};  // textos no interactivos
    int focus_ = Export;  // elemento con foco del teclado; arranca en "Exportar" (Enter directo)
    int hover_ = -1;      // elemento bajo el mouse (-1 = ninguno)
    bool done_ = false;   // true termina el bucle modal de Run()
    bool result_ = false; // true = el usuario eligió exportar
};

}  // namespace

bool ShowExportDialog(HWND owner, const std::vector<ExportDay>& days, ExportChoice& choice) {
    // La elección inicial se normaliza contra los días disponibles antes de
    // mostrarla; choice solo se sobrescribe si el usuario confirma.
    ExportChoice start = choice;
    NormalizeExportChoice(start, days.size(), true);
    ExportDialog dialog(days, start);
    if (!dialog.Run(owner)) return false;
    choice = dialog.choice();
    return true;
}

}  // namespace cola::ui
