// Panel de ayuda y reasignación de atajos (ver HelpOverlay.h). Se pinta a
// mano con GDI (doble búfer) sobre el cuerpo de la ventana principal y maneja
// por sí mismo el teclado, la rueda, el clic y su barra de desplazamiento.
#include "ui/HelpOverlay.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n/Strings.h"
#include "ui/Gfx.h"
#include "ui/Style.h"
#include "util/Strings.h"

namespace cola::ui {

using i18n::S;
using i18n::Tr;
using input::Command;
using input::kCommandCount;

bool HelpOverlay::Create(HWND parent, const input::ShortcutMap* shortcuts, Callbacks callbacks) {
    shortcuts_ = shortcuts;
    callbacks_ = std::move(callbacks);
    // Sin WS_VISIBLE: arranca oculto. WS_CLIPSIBLINGS evita que la lista y el
    // panel de detalles (hermanos que quedan debajo) pinten encima de él.
    return CreateWin(L"ColaHelpOverlay", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, 0, parent);
}

void HelpOverlay::Open() {
    // Cada apertura empieza limpia: sin captura a medias ni mensajes viejos.
    capturing_ = false;
    message_.clear();
    wheelRemainder_ = 0;
    EnsureVisible();
    // HWND_TOP: queda por encima de la lista y del panel en el orden Z.
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetFocus(hwnd_);
    Invalidate();
}

void HelpOverlay::Close() {
    capturing_ = false;
    Show(false);
}

int HelpOverlay::RowHeight() const { return Style::Get().Px(28); }

RECT HelpOverlay::CardRect(const RECT& client) const {
    // Tarjeta centrada: tan grande como permita la ventana (16 px de margen
    // por lado) pero sin pasar de 620×640 px lógicos.
    const auto& style = Style::Get();
    int w = std::min(gfx::Width(client) - style.Px(32), style.Px(620));
    int h = std::min(gfx::Height(client) - style.Px(32), style.Px(640));
    int x = (client.left + client.right - w) / 2;
    int y = (client.top + client.bottom - h) / 2;
    return {x, y, x + w, y + h};
}

void HelpOverlay::EnsureVisible() {
    // Si la fila elegida quedó arriba de lo visible, se sube; si quedó
    // abajo, se baja lo justo para que sea la última visible.
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visibleRows_) scroll_ = selected_ - visibleRows_ + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, kCommandCount - visibleRows_));
}

bool HelpOverlay::HandleKey(UINT vk) {
    // Modo captura: la próxima combinación se asigna al comando elegido.
    if (capturing_) {
        // Esc cancela la reasignación (no cierra el panel).
        if (vk == VK_ESCAPE) {
            capturing_ = false;
            message_.clear();
            Invalidate();
            return true;
        }
        // Supr o Retroceso durante la captura: dejar el comando sin atajo.
        if (vk == VK_DELETE || vk == VK_BACK) {
            if (callbacks_.clear) callbacks_.clear(static_cast<Command>(selected_));
            capturing_ = false;
            message_.clear();
            Invalidate();
            return true;
        }
        // Ignora las teclas modificadoras solas: se espera la combinación completa.
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN ||
            vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LSHIFT || vk == VK_RSHIFT)
            return true;
        // FromKeyEvent lee el estado de Ctrl/Mayús/Alt en este momento y lo
        // combina con la tecla. El mensaje de vuelta (conflicto, etc.) se
        // muestra en el pie.
        input::KeyChord chord = input::KeyChord::FromKeyEvent(vk);
        capturing_ = false;
        message_ = callbacks_.assign ? callbacks_.assign(static_cast<Command>(selected_), chord) : L"";
        Invalidate();
        return true;
    }

    // Modo normal: navegación por la lista y acciones sobre la fila elegida.
    switch (vk) {
        // Esc y F1 (la misma tecla que lo abre) cierran el panel.
        case VK_ESCAPE:
        case VK_F1:
            if (callbacks_.close) callbacks_.close();
            return true;
        case VK_UP: selected_ = std::max(0, selected_ - 1); break;
        case VK_DOWN: selected_ = std::min(kCommandCount - 1, selected_ + 1); break;
        // RePág / AvPág saltan una página (las filas visibles).
        case VK_PRIOR: selected_ = std::max(0, selected_ - visibleRows_); break;
        case VK_NEXT: selected_ = std::min(kCommandCount - 1, selected_ + visibleRows_); break;
        case VK_HOME: selected_ = 0; break;
        case VK_END: selected_ = kCommandCount - 1; break;
        case VK_RETURN: StartCapture(); break;
        case VK_DELETE:
            if (callbacks_.clear) callbacks_.clear(static_cast<Command>(selected_));
            message_.clear();
            break;
        case 'R':
            if (callbacks_.resetDefaults) callbacks_.resetDefaults();
            message_.clear();
            break;
        default: return true;  // el panel es modal: consume todo
    }
    EnsureVisible();
    Invalidate();
    return true;
}

void HelpOverlay::StartCapture() {
    capturing_ = true;
    // El pie pasa a decir "Presione la nueva combinación para «<comando>»...".
    message_ = util::Format(Tr(S::ShortcutPrompt), Tr(input::GetCommandInfo(static_cast<Command>(selected_)).label));
    SetFocus(hwnd_);
    Invalidate();
}

RECT HelpOverlay::ScrollThumbRect() const {
    // Sin barra si todo cabe (o si todavía no se pintó y no hay listArea_).
    if (kCommandCount <= visibleRows_ || gfx::Height(listArea_) <= 0) return RECT{};
    const auto& style = Style::Get();
    const int width = style.Px(8);
    // La pista ocupa el alto de la lista menos 2 px arriba y abajo. El
    // pulgar es proporcional a la fracción visible (mínimo 28 px para poder
    // agarrarlo) y su posición, proporcional a scroll_ sobre el máximo.
    const int trackTop = listArea_.top + style.Px(2);
    const int trackHeight = gfx::Height(listArea_) - style.Px(4);
    const int thumbHeight = std::max(style.Px(28), trackHeight * visibleRows_ / kCommandCount);
    const int maxScroll = kCommandCount - visibleRows_;
    const int top = trackTop + (maxScroll > 0 ? (trackHeight - thumbHeight) * scroll_ / maxScroll : 0);
    const int right = listArea_.right - style.Px(4);
    return RECT{right - width, top, right, top + thumbHeight};
}

void HelpOverlay::ScrollTo(int firstRow) {
    // A diferencia de EnsureVisible, no persigue a la fila elegida: la rueda y
    // la barra pueden alejarse de ella.
    scroll_ = std::clamp(firstRow, 0, std::max(0, kCommandCount - visibleRows_));
    Invalidate();
}

bool HelpOverlay::OnScrollBar(POINT pt) const {
    RECT thumb = ScrollThumbRect();
    if (thumb.bottom <= thumb.top) return false;
    // Zona de agarre de 20 px a la derecha de la lista (el pulgar mide 8).
    return pt.x >= thumb.right - Style::Get().Px(20) && pt.x <= listArea_.right && pt.y >= listArea_.top &&
           pt.y <= listArea_.bottom;
}

int HelpOverlay::RowAt(POINT pt) const {
    // Fuera de la lista o sobre la barra no hay fila.
    if (!gfx::Contains(listArea_, pt) || OnScrollBar(pt)) return -1;
    int index = scroll_ + (pt.y - listArea_.top) / RowHeight();
    // El espacio que sobra bajo la última fila visible tampoco cuenta.
    if (index >= kCommandCount || index >= scroll_ + visibleRows_) return -1;
    return index;
}

void HelpOverlay::Paint(HDC dc, const RECT& client) {
    const auto& style = Style::Get();
    const auto& pal = style.palette();
    const auto& fonts = style.fonts();
    // Fondo "velo": el color de la ventana oscurecido (más en modo oscuro)
    // para que la tarjeta resalte como un diálogo.
    gfx::Fill(dc, client, Blend(pal.window, pal.dark ? RGB(0, 0, 0) : RGB(40, 44, 52), pal.dark ? 0.45 : 0.28));

    RECT card = CardRect(client);
    gfx::FillRounded(dc, card, style.Px(12), pal.overlayScrim);
    gfx::StrokeRounded(dc, card, style.Px(12), pal.border);

    const int pad = style.Px(22);
    RECT title{card.left + pad, card.top + style.Px(16), card.right - pad, card.top + style.Px(44)};
    gfx::Text(dc, Tr(S::HelpTitle), title, fonts.title, pal.text);

    // Pie de dos líneas: la ayuda de uso y las teclas fijas (no reasignables);
    // si hay un mensaje (captura en curso o resultado), se muestra en su
    // lugar con el color de acento.
    RECT footer{card.left + pad, card.bottom - style.Px(62), card.right - pad, card.bottom - style.Px(12)};
    std::wstring footerText = message_.empty() ? std::wstring(Tr(S::HelpFooter)) + L"\n" + Tr(S::HelpFixedKeys) : message_;
    gfx::Text(dc, footerText, footer, fonts.small, message_.empty() ? pal.textMuted : pal.accent,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    // La lista ocupa lo que queda entre el título y el pie. Las filas que
    // caben dependen del tamaño de la ventana, por eso se recalculan aquí.
    RECT listArea{card.left + style.Px(10), title.bottom + style.Px(10), card.right - style.Px(10), footer.top - style.Px(8)};
    visibleRows_ = std::max(1, gfx::Height(listArea) / RowHeight());
    listArea_ = listArea;
    // Solo se ajusta al rango válido: si se forzara a mostrar la fila elegida,
    // la rueda y la barra no podrían alejarse de ella (la lista "rebotaba").
    scroll_ = std::clamp(scroll_, 0, std::max(0, kCommandCount - visibleRows_));

    // Una fila por comando visible: nombre a la izquierda y sus combinaciones
    // a la derecha (en una "pastilla") o "sin asignar" atenuado.
    for (int i = scroll_; i < kCommandCount && i < scroll_ + visibleRows_; ++i) {
        int top = listArea.top + (i - scroll_) * RowHeight();
        RECT row{listArea.left, top, listArea.right, top + RowHeight()};
        const bool sel = i == selected_;
        // Fila elegida: color de selección, teñido de acento mientras se
        // espera la nueva combinación (para que se note el modo captura).
        if (sel) gfx::FillRounded(dc, row, style.Px(6), capturing_ ? Blend(pal.selection, pal.accent, 0.25) : pal.selection);
        else if (i == hover_) gfx::FillRounded(dc, row, style.Px(6), pal.hover);
        const auto& info = input::GetCommandInfo(static_cast<Command>(i));
        // El nombre deja libres 200 px a la derecha para las teclas.
        RECT label{row.left + style.Px(12), row.top, row.right - style.Px(200), row.bottom};
        gfx::Text(dc, Tr(info.label), label, fonts.normal, sel ? pal.selectionText : pal.text);

        std::wstring keys = input::FormatChordList(shortcuts_->Get(static_cast<Command>(i)));
        bool none = keys.empty();
        if (none) keys = Tr(S::HelpUnassigned);
        RECT keyRect{row.right - style.Px(196), row.top, row.right - style.Px(20), row.bottom};
        SIZE sz = gfx::MeasureText(dc, keys, fonts.small);
        if (!none) {
            // Pastilla ajustada al texto y alineada a la derecha; si el
            // texto no cabe, se recorta con "…" dentro de keyRect.
            RECT chip{keyRect.right - sz.cx - style.Px(14), row.top + style.Px(4), keyRect.right, row.bottom - style.Px(4)};
            chip.left = std::max(chip.left, keyRect.left);
            gfx::FillRounded(dc, chip, style.Px(5), sel ? pal.surface : pal.surfaceAlt);
            gfx::StrokeRounded(dc, chip, style.Px(5), pal.border);
            gfx::Text(dc, keys, chip, fonts.small, pal.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
            gfx::Text(dc, keys, keyRect, fonts.small, pal.textMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    // Barra de desplazamiento (indica que la lista sigue más abajo).
    // Pista tenue a todo el alto y pulgar encima (acentuado mientras se arrastra).
    RECT thumb = ScrollThumbRect();
    if (thumb.bottom > thumb.top) {
        const int width = gfx::Width(thumb);
        RECT track{thumb.left, listArea.top + style.Px(2), thumb.right, listArea.bottom - style.Px(2)};
        gfx::FillRounded(dc, track, width / 2, Blend(pal.overlayScrim, pal.border, 0.6));
        gfx::FillRounded(dc, thumb, width / 2, draggingThumb_ ? pal.accent : pal.textMuted);
    }
}

LRESULT HelpOverlay::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            gfx::BufferedPaint paint(hwnd_);
            Paint(paint.dc(), paint.rect());
            return 0;
        }
        // Paint() cubre todo el fondo: no borrar evita parpadeo.
        case WM_ERASEBKGND: return 1;
        // La tarjeta y las filas visibles dependen del tamaño.
        case WM_SIZE: Invalidate(); return 0;
        // Pide todas las teclas (también Tab, Enter y Esc) para que no las
        // procese el sistema como navegación de diálogo.
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
        // WM_SYSKEYDOWN también: durante la captura se pueden asignar
        // combinaciones con Alt.
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            HandleKey(static_cast<UINT>(wParam));
            return 0;
        case WM_MOUSEWHEEL: {
            // 3 filas por muesca; los giros parciales se acumulan.
            wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wParam) * 3;
            const int rows = wheelRemainder_ / WHEEL_DELTA;
            wheelRemainder_ -= rows * WHEEL_DELTA;
            // Delta positivo = rueda hacia arriba = mostrar filas anteriores.
            if (rows) ScrollTo(scroll_ - rows);
            return 0;
        }
        // Fin del arrastre del pulgar.
        case WM_LBUTTONUP:
            if (draggingThumb_) {
                draggingThumb_ = false;
                ReleaseCapture();
                Invalidate();
            }
            return 0;
        // Otra ventana tomó la captura del mouse (p. ej. Alt+Tab durante el
        // arrastre): se da el arrastre por terminado.
        case WM_CAPTURECHANGED:
            if (draggingThumb_) {
                draggingThumb_ = false;
                Invalidate();
            }
            return 0;
        case WM_MOUSEMOVE: {
            // Arrastrando el pulgar: la posición del mouse dentro de la pista
            // (menos el punto donde se lo agarró) se traduce
            // proporcionalmente a la primera fila visible.
            if (draggingThumb_) {
                const auto& style = Style::Get();
                const int trackTop = listArea_.top + style.Px(2);
                const int trackHeight = gfx::Height(listArea_) - style.Px(4);
                const int thumbHeight = gfx::Height(ScrollThumbRect());
                const int span = std::max(1, trackHeight - thumbHeight);
                const int y = GET_Y_LPARAM(lParam) - thumbGrabOffset_ - trackTop;
                ScrollTo((kCommandCount - visibleRows_) * std::clamp(y, 0, span) / span);
                return 0;
            }
            // Pide WM_MOUSELEAVE una sola vez por entrada del mouse.
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            // Resaltado de la fila bajo el cursor (manito sobre las filas).
            int row = RowAt({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (row != hover_) {
                hover_ = row;
                SetCursor(LoadCursorW(nullptr, row >= 0 ? IDC_HAND : IDC_ARROW));
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hover_ = -1;
            Invalidate();
            return 0;
        // Mantiene la manito sobre una fila (si no, Windows repone la flecha
        // de la clase en cada movimiento).
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && hover_ >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            RECT client;
            GetClientRect(hwnd_, &client);
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetFocus(hwnd_);
            // Clic en el velo, fuera de la tarjeta: cerrar.
            if (!gfx::Contains(CardRect(client), pt)) {
                if (callbacks_.close) callbacks_.close();
                return 0;
            }
            // Barra de desplazamiento: arrastrar el pulgar o saltar en la pista.
            if (OnScrollBar(pt)) {
                RECT thumb = ScrollThumbRect();
                if (pt.y >= thumb.top && pt.y < thumb.bottom) {
                    // Sobre el pulgar: empieza el arrastre. La captura hace
                    // que los movimientos lleguen aunque el mouse salga del panel.
                    draggingThumb_ = true;
                    thumbGrabOffset_ = pt.y - thumb.top;
                    SetCapture(hwnd_);
                } else {
                    // En la pista: salta una página hacia el lado del clic.
                    ScrollTo(scroll_ + (pt.y < thumb.top ? -visibleRows_ : visibleRows_));
                }
                Invalidate();
                return 0;
            }
            int row = RowAt(pt);
            if (row < 0) return 0;
            // Clic sobre la zona de la tecla (a la derecha) o sobre la fila ya elegida,
            // o doble clic: empieza a reasignar. Si no, solo la selecciona.
            const bool onKeys = pt.x >= listArea_.right - Style::Get().Px(200);
            const bool again = row == selected_ && !capturing_;
            selected_ = row;
            if (msg == WM_LBUTTONDBLCLK || onKeys || again) {
                StartCapture();
            } else {
                // Elegir otra fila cancela una captura en curso.
                capturing_ = false;
                message_.clear();
                Invalidate();
            }
            return 0;
        }
    }
    return Window::HandleMessage(msg, wParam, lParam);
}

}  // namespace cola::ui
