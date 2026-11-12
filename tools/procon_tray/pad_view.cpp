#include "pad_view.h"

#include <algorithm>
#include <cmath>

using namespace procon;

namespace {

// Tela lógica de 440 x 280; o desenho escala para o tamanho do controle.
constexpr float kW = 440, kH = 280;

enum class Shape { Circle, Rect };

struct ButtonShape {
  Source src;
  Shape shape;
  float x, y;  // centro
  float w, h;  // largura/altura (círculo: w = diâmetro)
  const wchar_t* text;
};

const ButtonShape kButtons[] = {
    {Source::ZL, Shape::Rect, 111, 16, 98, 20, L"ZL"},
    {Source::L, Shape::Rect, 111, 40, 98, 20, L"L"},
    {Source::ZR, Shape::Rect, 329, 16, 98, 20, L"ZR"},
    {Source::R, Shape::Rect, 329, 40, 98, 20, L"R"},
    {Source::Minus, Shape::Circle, 178, 80, 20, 20, L"\x2212"},
    {Source::Plus, Shape::Circle, 262, 80, 20, 20, L"+"},
    {Source::Capture, Shape::Rect, 195, 118, 20, 20, L"\x25CF"},
    {Source::Home, Shape::Circle, 245, 118, 24, 24, L"\x2302"},
    {Source::LStick, Shape::Circle, 118, 104, 54, 54, L"L3"},
    {Source::RStick, Shape::Circle, 272, 172, 54, 54, L"R3"},
    {Source::Up, Shape::Rect, 170, 150, 20, 20, L"\x2191"},
    {Source::Down, Shape::Rect, 170, 194, 20, 20, L"\x2193"},
    {Source::Left, Shape::Rect, 148, 172, 20, 20, L"\x2190"},
    {Source::Right, Shape::Rect, 192, 172, 20, 20, L"\x2192"},
    {Source::X, Shape::Circle, 324, 80, 26, 26, L"X"},
    {Source::B, Shape::Circle, 324, 128, 26, 26, L"B"},
    {Source::Y, Shape::Circle, 300, 104, 26, 26, L"Y"},
    {Source::A, Shape::Circle, 348, 104, 26, 26, L"A"},
};
static_assert(sizeof(kButtons) / sizeof(kButtons[0]) == kSources, "um desenho por botão");

struct View {
  HWND wnd = nullptr;
  uint32_t pressed = 0;
  std::array<bool, kSources> changed{};
  int hover = -1;
  bool tracking = false;
};
View g;

struct Xf {
  float scale, ox, oy;
  int X(float x) const { return static_cast<int>(std::lround(ox + x * scale)); }
  int Y(float y) const { return static_cast<int>(std::lround(oy + y * scale)); }
  int L(float v) const { return static_cast<int>(std::lround(v * scale)); }
};

Xf Transform(HWND wnd) {
  RECT r;
  GetClientRect(wnd, &r);
  float s = std::fmin(r.right / kW, r.bottom / kH);
  return {s, (r.right - kW * s) / 2, (r.bottom - kH * s) / 2};
}

int HitTest(HWND wnd, int px, int py) {
  Xf t = Transform(wnd);
  if (t.scale <= 0) return -1;
  float x = (px - t.ox) / t.scale, y = (py - t.oy) / t.scale;
  for (const auto& b : kButtons) {
    float dx = x - b.x, dy = y - b.y;
    bool in = b.shape == Shape::Circle
                  ? dx * dx + dy * dy <= (b.w / 2 + 2) * (b.w / 2 + 2)
                  : std::fabs(dx) <= b.w / 2 + 2 && std::fabs(dy) <= b.h / 2 + 2;
    if (in) return static_cast<int>(b.src);
  }
  return -1;
}

void Paint(HWND wnd, HDC dc) {
  RECT rc;
  GetClientRect(wnd, &rc);
  FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
  Xf t = Transform(wnd);

  // Corpo: retângulo arredondado + as duas pegadas, como uma peça só.
  HRGN body = CreateRoundRectRgn(t.X(40), t.Y(52), t.X(400), t.Y(182), t.L(80), t.L(80));
  HRGN gl = CreateEllipticRgn(t.X(42), t.Y(118), t.X(168), t.Y(272));
  HRGN gr = CreateEllipticRgn(t.X(272), t.Y(118), t.X(398), t.Y(272));
  CombineRgn(body, body, gl, RGN_OR);
  CombineRgn(body, body, gr, RGN_OR);
  HBRUSH body_brush = CreateSolidBrush(RGB(58, 60, 66));
  HBRUSH edge = CreateSolidBrush(RGB(28, 28, 32));
  FillRgn(dc, body, body_brush);
  FrameRgn(dc, body, edge, 2, 2);
  DeleteObject(gl);
  DeleteObject(gr);
  DeleteObject(body);

  LOGFONTW lf{};
  lf.lfHeight = -std::max(9, t.L(12));
  lf.lfWeight = FW_SEMIBOLD;
  lf.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(lf.lfFaceName, L"Segoe UI");
  HFONT font = CreateFontIndirectW(&lf);
  HGDIOBJ old_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);

  for (const auto& b : kButtons) {
    int i = static_cast<int>(b.src);
    bool down = (g.pressed & SourceBit(b.src)) != 0;
    COLORREF fill = down ? RGB(0, 120, 215) : RGB(30, 31, 35);
    COLORREF line = i == g.hover ? RGB(255, 196, 0) : g.changed[i] ? RGB(255, 140, 0)
                                                                    : RGB(110, 112, 120);
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, t.L(i == g.hover ? 3.0f : 1.5f)), line);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    int l = t.X(b.x - b.w / 2), r = t.X(b.x + b.w / 2);
    int tp = t.Y(b.y - b.h / 2), bt = t.Y(b.y + b.h / 2);
    if (b.shape == Shape::Circle) Ellipse(dc, l, tp, r, bt);
    else RoundRect(dc, l, tp, r, bt, t.L(8), t.L(8));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);

    RECT tr{l, tp, r, bt};
    SetTextColor(dc, RGB(235, 235, 240));
    DrawTextW(dc, b.text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  SelectObject(dc, old_font);
  DeleteObject(font);
  DeleteObject(body_brush);
  DeleteObject(edge);
}

void Notify(HWND wnd, WORD code) {
  HWND parent = GetParent(wnd);
  SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(wnd), code),
               reinterpret_cast<LPARAM>(wnd));
}

LRESULT CALLBACK PadProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(wnd, &ps);
      RECT rc;
      GetClientRect(wnd, &rc);
      // Desenha fora da tela e copia, para não piscar.
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
      HGDIOBJ old = SelectObject(mem, bmp);
      Paint(wnd, mem);
      BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(wnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEMOVE: {
      if (!g.tracking) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd, 0};
        TrackMouseEvent(&tme);
        g.tracking = true;
      }
      int h = HitTest(wnd, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
      if (h != g.hover) {
        g.hover = h;
        InvalidateRect(wnd, nullptr, FALSE);
        Notify(wnd, kPadHover);
      }
      SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(h >= 0 ? 32649 : 32512)));  // mão / seta
      return 0;
    }
    case WM_SETCURSOR:
      return TRUE;  // o WM_MOUSEMOVE escolhe o cursor
    case WM_MOUSELEAVE:
      g.tracking = false;
      if (g.hover != -1) {
        g.hover = -1;
        InvalidateRect(wnd, nullptr, FALSE);
        Notify(wnd, kPadHover);
      }
      return 0;
    case WM_LBUTTONUP:
      if (g.hover >= 0) Notify(wnd, kPadClick);
      return 0;
    case WM_DESTROY:
      g = View{};
      return 0;
  }
  return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

HWND CreatePadView(HWND parent, int id, int x, int y, int w, int h) {
  static bool registered = false;
  HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = PadProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"ProconWinPad";
    RegisterClassW(&wc);
    registered = true;
  }
  g.wnd = CreateWindowExW(0, L"ProconWinPad", L"", WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
  return g.wnd;
}

void PadViewSet(HWND view, uint32_t pressed, const std::array<bool, kSources>& changed) {
  if (pressed == g.pressed && changed == g.changed) return;
  g.pressed = pressed;
  g.changed = changed;
  InvalidateRect(view, nullptr, FALSE);
}

int PadViewHover(HWND) { return g.hover; }
