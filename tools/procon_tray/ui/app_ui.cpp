#include "app_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "../labels.h"

using namespace procon;

namespace ui {

namespace {

// --- cores ------------------------------------------------------------------

ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
ImVec4 V(int r, int g, int b, int a = 255) {
  return ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
}

const ImVec4 kBg = V(18, 20, 25);
const ImVec4 kCard = V(28, 31, 38);
const ImVec4 kCardBorder = V(42, 46, 55);
const ImVec4 kText = V(230, 232, 236);
const ImVec4 kMuted = V(150, 157, 170);
const ImVec4 kAccent = V(79, 140, 255);
const ImVec4 kAccentHover = V(106, 158, 255);
const ImVec4 kControl = V(40, 44, 53);
const ImVec4 kControlHover = V(50, 55, 66);
const ImU32 kSuccess = Rgb(52, 199, 123);
const ImU32 kWarn = Rgb(245, 165, 36);
const ImU32 kDanger = Rgb(242, 95, 92);
const ImU32 kChanged = Rgb(255, 140, 50);
const ImU32 kHover = Rgb(255, 197, 61);

constexpr float kPcSlow = 900.0f, kPcNormal = 1600.0f, kPcFast = 2600.0f;

// --- estado da janela -------------------------------------------------------

struct State {
  bool pc_view = false;
  int hover = -1;      // botão do desenho sob o mouse
  int picking = -1;    // botão cujo menu de função está aberto
  char new_name[64] = {};
  int new_start = 0;   // 0 = botões atuais, 1 = padrão posição, 2 = padrão nome
  std::string new_error;
  bool confirm_replace = false;
  bool open_new = false, open_delete = false, open_diag = false;
  std::string notice;  // aviso curto embaixo do perfil
};
State g;

float S(float v, float scale) { return v * scale; }

// --- widgets ----------------------------------------------------------------

void SectionTitle(const Fonts& f, const char* text) {
  ImGui::PushFont(f.bold);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
  ImGui::Spacing();
}

void Muted(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// Botão de destaque (azul).
bool AccentButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
  ImGui::PushStyleColor(ImGuiCol_Text, V(255, 255, 255));
  bool r = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return r;
}

// Opções lado a lado; retorna o índice escolhido.
int Segmented(const char* id, const char* const* items, int n, int sel, float width,
              bool enabled = true) {
  ImGui::PushID(id);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float h = ImGui::GetFrameHeight();
  float r = ImGui::GetStyle().FrameRounding;
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), ImGui::GetColorU32(kControl), r);
  float w = width / n;
  int result = sel;
  if (!enabled) ImGui::BeginDisabled();
  for (int i = 0; i < n; ++i) {
    ImGui::SetCursorScreenPos(ImVec2(p.x + w * i, p.y));
    bool on = i == sel;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? kAccent : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kAccentHover : kControlHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? kAccent : kControlHover);
    ImGui::PushStyleColor(ImGuiCol_Text, on ? V(255, 255, 255) : kText);
    ImGui::PushID(i);
    if (ImGui::Button(items[i], ImVec2(w, h))) result = i;
    ImGui::PopID();
    ImGui::PopStyleColor(4);
  }
  if (!enabled) ImGui::EndDisabled();
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + ImGui::GetStyle().ItemSpacing.y));
  ImGui::Dummy(ImVec2(width, 0));
  ImGui::PopID();
  return result;
}

// Interruptor estilo celular. Retorna true se mudou.
bool Toggle(const char* id, bool* v, bool enabled = true) {
  float h = ImGui::GetFrameHeight() * 0.8f;
  float w = h * 1.8f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, ImVec2(w, h));
  bool changed = false;
  if (enabled && ImGui::IsItemClicked()) {
    *v = !*v;
    changed = true;
  }
  if (enabled && ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 bg = *v ? ImGui::GetColorU32(kAccent) : Rgb(62, 68, 80);
  if (!enabled) bg = Rgb(48, 52, 60);
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h / 2);
  float knob = h / 2 - 3;
  float x = *v ? p.x + w - h / 2 : p.x + h / 2;
  dl->AddCircleFilled(ImVec2(x, p.y + h / 2), knob, enabled ? Rgb(255, 255, 255) : Rgb(140, 145, 155));
  return changed;
}

// Linha de opção: título, descrição e o interruptor à direita.
bool OptionRow(const Fonts& f, const char* title, const char* desc, bool* v, bool enabled = true) {
  ImGui::PushID(title);
  float right = ImGui::GetContentRegionAvail().x;
  float toggle_w = ImGui::GetFrameHeight() * 0.8f * 1.8f;
  ImVec2 start = ImGui::GetCursorPos();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + right - toggle_w - 16);
  if (!enabled) ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
  ImGui::PushFont(f.regular);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  if (!enabled) ImGui::PopStyleColor();
  if (desc && *desc) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", desc);
    ImGui::PopStyleColor();
  }
  ImGui::PopTextWrapPos();
  ImVec2 end = ImGui::GetCursorPos();
  float toggle_h = ImGui::GetFrameHeight() * 0.8f;
  ImGui::SetCursorPos(
      ImVec2(start.x + right - toggle_w, start.y + (ImGui::GetTextLineHeight() - toggle_h) / 2));
  bool changed = Toggle("##t", v, enabled);
  ImGui::SetCursorPos(ImVec2(start.x, std::max(end.y, ImGui::GetCursorPos().y)));
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::PopID();
  return changed;
}

// Cartão: fundo arredondado com borda.
bool BeginCard(const char* id, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
  ImGui::PushStyleColor(ImGuiCol_Border, kCardBorder);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding * 2);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.1f,
                                                          ImGui::GetFontSize() * 1.0f));
  bool open = ImGui::BeginChild(id, size,
                                ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
  return open;
}
void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

// --- desenho do controle ----------------------------------------------------

// Tela lógica de 440 x 290; tudo escala junto.
constexpr float kPadW = 440, kPadH = 290;

enum class Shape { Circle, Rect, Pill };

struct PadButton {
  Source src;
  Shape shape;
  float x, y, w, h;
  const char* text;
};

const PadButton kPadButtons[] = {
    {Source::ZL, Shape::Rect, 118, 40, 84, 30, "ZL"},
    {Source::ZR, Shape::Rect, 322, 40, 84, 30, "ZR"},
    {Source::L, Shape::Rect, 120, 68, 104, 24, "L"},
    {Source::R, Shape::Rect, 320, 68, 104, 24, "R"},
    {Source::Minus, Shape::Pill, 186, 96, 22, 10, u8"−"},
    {Source::Plus, Shape::Circle, 254, 96, 20, 20, "+"},
    {Source::Capture, Shape::Rect, 194, 128, 17, 17, ""},
    {Source::Home, Shape::Circle, 246, 128, 22, 22, ""},
    {Source::LStick, Shape::Circle, 122, 122, 60, 60, "L3"},
    {Source::RStick, Shape::Circle, 282, 196, 60, 60, "R3"},
    {Source::Up, Shape::Rect, 160, 172, 20, 22, ""},
    {Source::Down, Shape::Rect, 160, 216, 20, 22, ""},
    {Source::Left, Shape::Rect, 138, 194, 22, 20, ""},
    {Source::Right, Shape::Rect, 182, 194, 22, 20, ""},
    {Source::X, Shape::Circle, 326, 96, 27, 27, "X"},
    {Source::B, Shape::Circle, 326, 148, 27, 27, "B"},
    {Source::Y, Shape::Circle, 300, 122, 27, 27, "Y"},
    {Source::A, Shape::Circle, 352, 122, 27, 27, "A"},
};
static_assert(sizeof(kPadButtons) / sizeof(kPadButtons[0]) == kSources, "um por botão");

struct Bez {
  ImVec2 c1, c2, p;
};

// Metade direita do contorno, de cima no meio até embaixo no meio.
const ImVec2 kBodyStart(220, 76);
const Bez kBodyRight[] = {
    {{260, 76}, {280, 74}, {306, 74}},
    {{342, 72}, {370, 78}, {386, 94}},
    {{402, 110}, {410, 136}, {416, 166}},
    {{424, 206}, {428, 246}, {408, 268}},
    {{392, 286}, {362, 286}, {348, 270}},
    {{336, 256}, {326, 240}, {310, 234}},
    {{290, 226}, {254, 228}, {220, 228}},
};

std::vector<ImVec2> BodyOutline() {
  std::vector<ImVec2> right;
  right.push_back(kBodyStart);
  ImVec2 cur = kBodyStart;
  for (const auto& b : kBodyRight) {
    for (int i = 1; i <= 16; ++i) {
      float t = i / 16.0f, u = 1 - t;
      float x = u * u * u * cur.x + 3 * u * u * t * b.c1.x + 3 * u * t * t * b.c2.x + t * t * t * b.p.x;
      float y = u * u * u * cur.y + 3 * u * u * t * b.c1.y + 3 * u * t * t * b.c2.y + t * t * t * b.p.y;
      right.push_back(ImVec2(x, y));
    }
    cur = b.p;
  }
  // Sentido horário: direita de cima para baixo, depois a esquerda espelhada
  // de baixo para cima.
  std::vector<ImVec2> all = right;
  for (int i = static_cast<int>(right.size()) - 2; i >= 1; --i) {
    all.push_back(ImVec2(kPadW - right[i].x, right[i].y));
  }
  return all;
}

struct PadXf {
  ImVec2 o;
  float s;
  ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s); }
  ImVec2 P(ImVec2 v) const { return P(v.x, v.y); }
};

int PadHitTest(const PadXf& t, ImVec2 m) {
  float x = (m.x - t.o.x) / t.s, y = (m.y - t.o.y) / t.s;
  // De trás para frente: L/R ficam por cima de ZL/ZR.
  for (int k = kSources - 1; k >= 0; --k) {
    const auto& b = kPadButtons[k];
    float dx = x - b.x, dy = y - b.y;
    bool in = b.shape == Shape::Circle
                  ? dx * dx + dy * dy <= (b.w / 2 + 3) * (b.w / 2 + 3)
                  : std::fabs(dx) <= b.w / 2 + 3 && std::fabs(dy) <= b.h / 2 + 3;
    if (in) return static_cast<int>(b.src);
  }
  return -1;
}

void DrawPad(ImDrawList* dl, const PadXf& t, const Fonts& f, const EngineStatus& st,
             const std::array<bool, kSources>& changed, int hover) {
  const float s = t.s;
  auto pressed = [&](Source src) { return st.connected && (st.buttons & SourceBit(src)); };

  auto shoulder = [&](const PadButton& b, ImU32 fill) {
    ImVec2 a = t.P(b.x - b.w / 2, b.y - b.h / 2), c = t.P(b.x + b.w / 2, b.y + b.h / 2);
    dl->AddRectFilled(a, c, fill, 10 * s);
  };

  // Gatilhos atrás do corpo.
  for (int k = 0; k < 2; ++k) {
    const auto& b = kPadButtons[k];
    bool down = pressed(b.src);
    shoulder(b, down ? ImGui::GetColorU32(kAccent) : Rgb(52, 57, 67));
  }

  // L e R entre os gatilhos e o corpo.
  for (int k = 2; k < 4; ++k) {
    const auto& b = kPadButtons[k];
    shoulder(b, pressed(b.src) ? ImGui::GetColorU32(kAccent) : Rgb(64, 70, 82));
  }

  // Sombra e corpo.
  std::vector<ImVec2> outline = BodyOutline();
  std::vector<ImVec2> pts(outline.size());
  for (int layer = 3; layer >= 1; --layer) {
    for (size_t i = 0; i < outline.size(); ++i) {
      pts[i] = t.P(outline[i].x, outline[i].y + 3.0f * layer);
    }
    dl->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), Rgb(0, 0, 0, 40));
  }
  for (size_t i = 0; i < outline.size(); ++i) pts[i] = t.P(outline[i]);
  dl->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), Rgb(46, 50, 59));
  dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), Rgb(70, 76, 88), ImDrawFlags_Closed,
                  1.5f * s);

  // Poços dos analógicos.
  for (Source src : {Source::LStick, Source::RStick}) {
    const auto& b = kPadButtons[static_cast<int>(src)];
    dl->AddCircleFilled(t.P(b.x, b.y), (b.w / 2 + 4) * s, Rgb(24, 26, 31), 48);
  }
  // Base da cruz do direcional.
  dl->AddRectFilled(t.P(147, 158), t.P(173, 230), Rgb(24, 26, 31), 7 * s);
  dl->AddRectFilled(t.P(124, 181), t.P(196, 207), Rgb(24, 26, 31), 7 * s);
  dl->AddRectFilled(t.P(150, 184), t.P(170, 204), Rgb(30, 33, 40), 3 * s);

  ImGui::PushFont(f.pad);
  for (int k = 0; k < kSources; ++k) {
    const auto& b = kPadButtons[k];
    int i = static_cast<int>(b.src);
    bool down = pressed(b.src);
    ImVec2 c = t.P(b.x, b.y);
    ImU32 fill = down ? ImGui::GetColorU32(kAccent) : Rgb(30, 33, 40);
    ImU32 ring = Rgb(78, 85, 98);
    bool stick = b.src == Source::LStick || b.src == Source::RStick;
    bool shoulder_btn = k < 4;

    if (stick) {
      // O chapéu do analógico acompanha o analógico de verdade.
      float ax = b.src == Source::LStick ? st.lx : st.rx;
      float ay = b.src == Source::LStick ? st.ly : st.ry;
      if (!st.connected) ax = ay = 0;
      ImVec2 cap(c.x + ax * 9 * s, c.y - ay * 9 * s);
      dl->AddCircleFilled(cap, b.w / 2 * 0.78f * s, down ? fill : Rgb(56, 61, 72), 48);
      dl->AddCircle(cap, b.w / 2 * 0.78f * s, Rgb(90, 97, 112), 48, 1.5f * s);
      dl->AddCircle(cap, b.w / 2 * 0.5f * s, Rgb(70, 76, 88), 48, 1.0f * s);
    } else if (!shoulder_btn) {
      ImVec2 a = t.P(b.x - b.w / 2, b.y - b.h / 2), z = t.P(b.x + b.w / 2, b.y + b.h / 2);
      switch (b.shape) {
        case Shape::Circle:
          dl->AddCircleFilled(c, b.w / 2 * s, fill, 40);
          dl->AddCircle(c, b.w / 2 * s, ring, 40, 1.2f * s);
          break;
        case Shape::Pill:
          dl->AddRectFilled(a, z, fill, b.h / 2 * s);
          break;
        case Shape::Rect:
          dl->AddRectFilled(a, z, fill, 4 * s);
          break;
      }
    }

    // Símbolos desenhados (HOME, Captura, setas).
    ImU32 ink = Rgb(225, 228, 235);
    if (b.src == Source::Home) {
      dl->AddCircleFilled(c, 4.0f * s, ink, 24);
    } else if (b.src == Source::Capture) {
      dl->AddCircle(c, 4.0f * s, ink, 24, 1.4f * s);
    } else if (b.src >= Source::Up && b.src <= Source::Right) {
      float r = 4.5f * s;
      ImVec2 p1, p2, p3;
      switch (b.src) {
        case Source::Up: p1 = {c.x, c.y - r}; p2 = {c.x - r, c.y + r * 0.6f}; p3 = {c.x + r, c.y + r * 0.6f}; break;
        case Source::Down: p1 = {c.x, c.y + r}; p2 = {c.x + r, c.y - r * 0.6f}; p3 = {c.x - r, c.y - r * 0.6f}; break;
        case Source::Left: p1 = {c.x - r, c.y}; p2 = {c.x + r * 0.6f, c.y + r}; p3 = {c.x + r * 0.6f, c.y - r}; break;
        default: p1 = {c.x + r, c.y}; p2 = {c.x - r * 0.6f, c.y - r}; p3 = {c.x - r * 0.6f, c.y + r}; break;
      }
      dl->AddTriangleFilled(p1, p2, p3, Rgb(160, 166, 178));
    } else if (*b.text) {
      ImVec2 ts = ImGui::CalcTextSize(b.text);
      float lift = k >= 2 && k < 4 ? -3 * s : 0;  // na parte que aparece
      dl->AddText(ImVec2(c.x - ts.x / 2, c.y - ts.y / 2 + lift), stick ? Rgb(150, 157, 170) : ink,
                  b.text);
    }

    // Contornos: amarelo = sob o mouse, laranja = fora do padrão.
    if (i == hover || changed[i]) {
      ImU32 col = i == hover ? kHover : kChanged;
      float th = (i == hover ? 2.5f : 1.8f) * s;
      if (b.shape == Shape::Circle) {
        dl->AddCircle(c, (b.w / 2 + 3) * s, col, 40, th);
      } else {
        dl->AddRect(t.P(b.x - b.w / 2 - 3, b.y - b.h / 2 - 3), t.P(b.x + b.w / 2 + 3, b.y + b.h / 2 + 3),
                    col, 7 * s, 0, th);
      }
    }
  }
  ImGui::PopFont();
}

// --- partes da janela -------------------------------------------------------

struct StatusLine {
  std::string text;
  ImU32 color;
};

StatusLine Describe(const EngineStatus& st) {
  if (!st.driver_error.empty()) return {u8"Driver do controle virtual não encontrado", kDanger};
  if (st.paused) {
    if (st.steam_game.empty()) return {u8"Pausado · a Steam está com o controle", kWarn};
    return {u8"Pausado · " + st.steam_game + " (Steam)", kWarn};
  }
  if (!st.connected) return {u8"Esperando o Pro Controller", Rgb(150, 157, 170)};
  std::string s = st.usb ? u8"Conectado · USB" : u8"Conectado · Bluetooth";
  if (!st.usb || st.charging) {
    s += u8" · bateria " + std::to_string(st.battery * 100 / 8) + "%";
    if (st.charging) s += " (carregando)";
  }
  if (st.pc_mode) s += u8" · modo computador";
  return {s, kSuccess};
}

void Header(UiModel& m, const Fonts& f, float scale) {
  ImGui::PushFont(f.title);
  ImGui::TextUnformatted("ProconWin");
  ImGui::PopFont();

  StatusLine sl = Describe(m.Status());
  ImGui::PushFont(f.bold);
  ImVec2 ts = ImGui::CalcTextSize(sl.text.c_str());
  ImGui::PopFont();
  float pad = S(14, scale), h = ts.y + S(12, scale), dot = S(5, scale);
  float w = ts.x + pad * 2 + dot * 3;
  ImVec2 win = ImGui::GetWindowPos();
  float right = win.x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
  ImVec2 p(right - w, ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - h) / 2);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 bg = (sl.color & 0x00FFFFFF) | (40u << 24);
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h / 2);
  dl->AddCircleFilled(ImVec2(p.x + pad + dot, p.y + h / 2), dot, sl.color);
  ImGui::PushFont(f.bold);
  dl->AddText(ImVec2(p.x + pad + dot * 3, p.y + (h - ts.y) / 2), Rgb(235, 237, 241),
              sl.text.c_str());
  ImGui::PopFont();
  ImGui::Spacing();
}

void ButtonsCard(UiModel& m, const Fonts& f, float scale, ImVec2 size) {
  BeginCard("##buttons", size);
  Settings c = m.Get();
  EngineStatus st = m.Status();

  SectionTitle(f, u8"Botões");
  static const char* views[] = {"Nos jogos (Xbox)", "Modo computador"};
  float avail = ImGui::GetContentRegionAvail().x;
  int v = Segmented("view", views, 2, g.pc_view ? 1 : 0, avail);
  g.pc_view = v == 1;

  // Área do desenho.
  float hint_h = ImGui::GetTextLineHeightWithSpacing() * 3 + ImGui::GetFrameHeightWithSpacing();
  ImVec2 area(avail, std::max(S(160, scale), ImGui::GetContentRegionAvail().y - hint_h));
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##pad", area);
  bool area_hovered = ImGui::IsItemHovered();
  float sc = std::min(area.x / kPadW, area.y / kPadH);
  PadXf t{ImVec2(p.x + (area.x - kPadW * sc) / 2, p.y + (area.y - kPadH * sc) / 2), sc};

  g.hover = area_hovered ? PadHitTest(t, ImGui::GetIO().MousePos) : -1;
  if (g.hover >= 0) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      g.picking = g.hover;
      ImGui::OpenPopup("pick");
    }
  }

  std::array<bool, kSources> changed{};
  ButtonMap def = DefaultPositionalMap();
  PcMap pdef = DefaultPcMap();
  for (int i = 0; i < kSources; ++i) {
    changed[i] = g.pc_view ? c.pc_buttons[i] != pdef[i] : c.buttons[i] != def[i];
  }
  DrawPad(ImGui::GetWindowDrawList(), t, f, st, changed, g.hover >= 0 ? g.hover : g.picking);

  // Escolher a função do botão clicado.
  ImGui::SetNextWindowSizeConstraints(ImVec2(S(220, scale), 0), ImVec2(S(320, scale), S(420, scale)));
  if (ImGui::BeginPopup("pick")) {
    int i = g.picking;
    if (i >= 0) {
      ImGui::PushFont(f.bold);
      ImGui::Text(u8"%s vai para:", SourceLabel(i));
      ImGui::PopFont();
      ImGui::Separator();
      int n = g.pc_view ? kPcActions : kTargets;
      int cur = g.pc_view ? static_cast<int>(c.pc_buttons[i]) : static_cast<int>(c.buttons[i]);
      for (int k = 0; k < n; ++k) {
        const char* label = g.pc_view ? PcLabel(k) : TargetLabel(k);
        if (ImGui::Selectable(label, k == cur)) {
          if (g.pc_view) c.pc_buttons[i] = static_cast<PcAction>(k);
          else c.buttons[i] = static_cast<Target>(k);
          m.Apply(c);
        }
        if (k == cur) ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndPopup();
  } else {
    g.picking = -1;
  }

  // Linha de ajuda / botão sob o mouse.
  int show = g.hover;
  if (show >= 0) {
    std::string map = g.pc_view ? PcLabel(static_cast<int>(c.pc_buttons[show]))
                                : TargetLabel(static_cast<int>(c.buttons[show]));
    ImGui::PushFont(f.bold);
    ImGui::Text(u8"%s  →  %s", SourceLabel(show), map.c_str());
    ImGui::PopFont();
    Muted("Clique para trocar.");
  } else if (g.pc_view) {
    Muted(u8"Analógico direito = mouse, esquerdo = rolar a página. Clique num botão do desenho "
          u8"para escolher o que ele faz. Vale para todos os perfis.");
  } else {
    Muted(u8"Clique num botão do desenho para escolher para qual botão do Xbox ele vai. O botão "
          u8"que você aperta no controle acende em azul; laranja = fora do padrão.");
  }

  ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y -
                       ImGui::GetFrameHeight());
  if (g.pc_view) {
    if (ImGui::Button(u8"Restaurar o padrão do modo computador")) {
      c.pc_buttons = DefaultPcMap();
      m.Apply(c);
    }
  } else {
    if (ImGui::Button(u8"Padrão: pela posição")) {
      c.buttons = DefaultPositionalMap();
      m.Apply(c);
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"Padrão: pelo nome (A = A)")) {
      c.buttons = NintendoLabelMap();
      m.Apply(c);
    }
  }
  EndCard();
}

void ProfileSection(UiModel& m, const Fonts& f, float scale) {
  Settings c = m.Get();
  SectionTitle(f, "Perfil");
  auto names = m.ListProfiles();
  const char* preview = c.profile.empty() ? "(nenhum)" : c.profile.c_str();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##perfil", preview)) {
    if (ImGui::Selectable("(nenhum)", c.profile.empty())) {
      c.profile.clear();
      m.Apply(c);
    }
    for (const auto& n : names) {
      if (ImGui::Selectable(n.c_str(), n == c.profile)) {
        Settings p;
        if (m.LoadProfile(n, p)) {
          ApplyProfile(p, c);
          c.profile = n;
          m.Apply(c);
        } else {
          g.notice = u8"Não consegui abrir esse perfil.";
        }
      }
    }
    ImGui::EndCombo();
  }
  float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  if (AccentButton("+  Novo perfil", ImVec2(w, 0))) {
    g.open_new = true;
    g.new_name[0] = 0;
    g.new_start = 0;
    g.new_error.clear();
    g.confirm_replace = false;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(c.profile.empty());
  if (ImGui::Button("Excluir perfil", ImVec2(w, 0))) g.open_delete = true;
  ImGui::EndDisabled();
  if (!g.notice.empty()) Muted(g.notice.c_str());
  (void)scale;
}

void GyroSection(UiModel& m, const Fonts& f) {
  Settings c = m.Get();
  SectionTitle(f, u8"Giroscópio");
  static const char* modes[] = {"Desligado", "Mouse", u8"Analógico"};
  int sel = c.gyro == GyroMode::Mouse ? 1 : c.gyro == GyroMode::RightStick ? 2 : 0;
  int now = Segmented("gyro", modes, 3, sel, ImGui::GetContentRegionAvail().x);
  if (now != sel) {
    c.gyro = now == 1 ? GyroMode::Mouse : now == 2 ? GyroMode::RightStick : GyroMode::Off;
    m.Apply(c);
  }
  bool off = c.gyro == GyroMode::Off;
  int level = c.gyro == GyroMode::RightStick ? StickLevelOf(c.stick_full_speed_dps)
                                             : MouseLevelOf(c.mouse_pixels_per_degree);
  ImGui::BeginDisabled(off);
  ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
  ImGui::TextUnformatted(off ? "Sensibilidade (ligue o giroscópio)" : "Sensibilidade");
  ImGui::PopStyleColor();
  ImGui::SetNextItemWidth(-1);
  int nl = level;
  if (ImGui::SliderInt("##sens", &nl, 1, kGyroLevels, "%d de 10") && nl != level) {
    if (c.gyro == GyroMode::RightStick) c.stick_full_speed_dps = StickSpeedForLevel(nl);
    else c.mouse_pixels_per_degree = MouseSensForLevel(nl);
    m.Apply(c);
  }
  ImGui::EndDisabled();
  if (ImGui::Button("Recalibrar (deixe o controle parado)")) m.Recalibrate();
}

void OptionsSection(UiModel& m, const Fonts& f) {
  Settings c = m.Get();
  SectionTitle(f, u8"Opções");
  bool v = c.rumble;
  if (OptionRow(f, u8"Vibração", u8"Deixa os jogos vibrarem o controle.", &v)) {
    c.rumble = v;
    m.Apply(c);
  }
  v = c.home_light;
  if (OptionRow(f, u8"Luz do botão HOME", nullptr, &v)) {
    c.home_light = v;
    m.Apply(c);
  }
  v = c.pc_mode;
  if (OptionRow(f, "Modo computador",
                u8"Mouse e teclado. Também liga e desliga segurando Captura 1 s.", &v)) {
    c.pc_mode = v;
    m.Apply(c);
  }
  {
    static const char* speeds[] = {"Lento", "Normal", u8"Rápido"};
    int sp = c.pc_mouse_speed < 1200 ? 0 : c.pc_mouse_speed < 2100 ? 1 : 2;
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextUnformatted("Velocidade do mouse no modo computador");
    ImGui::PopStyleColor();
    int ns = Segmented("pcspeed", speeds, 3, sp, ImGui::GetContentRegionAvail().x);
    if (ns != sp) {
      c.pc_mouse_speed = ns == 0 ? kPcSlow : ns == 2 ? kPcFast : kPcNormal;
      m.Apply(c);
    }
    ImGui::Dummy(ImVec2(0, 4));
  }
  bool using_steam = c.steam == SteamMode::Always;
  v = using_steam;
  if (OptionRow(f, "Estou usando a Steam", u8"Pausa o ProconWin para o Steam Input usar o controle.",
                &v)) {
    c.steam = v ? SteamMode::Always : m.SteamBase();
    m.Apply(c);
  }
  v = m.SteamBase() == SteamMode::Auto;
  if (OptionRow(f, "Pausar sozinho com jogos da Steam", nullptr, &v, !using_steam)) {
    SteamMode base = v ? SteamMode::Auto : SteamMode::Never;
    m.SetSteamBase(base);
    if (c.steam != SteamMode::Always) c.steam = base;
    m.Apply(c);
  }
  v = m.Autostart();
  if (OptionRow(f, "Iniciar com o Windows", u8"Abre só na bandeja, ao lado do relógio.", &v)) {
    m.SetAutostart(v);
  }
}

void Modals(UiModel& m, const Fonts& f, float scale) {
  if (g.open_new) ImGui::OpenPopup("Novo perfil");
  if (g.open_delete) ImGui::OpenPopup("Excluir perfil");
  if (g.open_diag) ImGui::OpenPopup(u8"Diagnóstico");
  g.open_new = g.open_delete = g.open_diag = false;

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(S(420, scale), 0));
  if (ImGui::BeginPopupModal("Novo perfil", nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::TextUnformatted("Nome do perfil (por exemplo, o nome do jogo)");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputText("##nome", g.new_name, sizeof(g.new_name),
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) g.confirm_replace = false;
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"Começar com:");
    ImGui::RadioButton(u8"Os botões de agora", &g.new_start, 0);
    ImGui::RadioButton(u8"Padrão: pela posição", &g.new_start, 1);
    ImGui::RadioButton(u8"Padrão: pelo nome (A = A)", &g.new_start, 2);
    if (!g.new_error.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kWarn));
      ImGui::TextWrapped("%s", g.new_error.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    float bw = S(120, scale);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - ImGui::GetStyle().ItemSpacing.x -
                         ImGui::GetStyle().WindowPadding.x);
    bool create = AccentButton(g.confirm_replace ? "Substituir" : "Criar perfil", ImVec2(bw, 0)) ||
                  enter;
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::CloseCurrentPopup();
    }
    if (create) {
      std::string name = SanitizeProfileName(g.new_name);
      bool exists = false;
      for (const auto& n : m.ListProfiles()) exists |= n == name;
      if (name.empty()) {
        g.new_error = "Digite um nome para o perfil.";
      } else if (exists && !g.confirm_replace) {
        g.confirm_replace = true;
        g.new_error = u8"Já existe um perfil com esse nome. Clique em Substituir para trocar.";
      } else {
        Settings c = m.Get();
        if (g.new_start == 1) c.buttons = DefaultPositionalMap();
        if (g.new_start == 2) c.buttons = NintendoLabelMap();
        c.profile = name;
        if (m.SaveProfile(name, c)) {
          m.Apply(c);
          g.pc_view = false;
          g.notice = u8"Perfil criado e em uso. Clique nos botões do desenho para escolher a "
                     u8"função de cada um: tudo fica salvo nele.";
          ImGui::CloseCurrentPopup();
        } else {
          g.new_error = u8"Não consegui salvar o perfil.";
        }
      }
    }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(S(380, scale), 0));
  if (ImGui::BeginPopupModal("Excluir perfil", nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    Settings c = m.Get();
    ImGui::TextWrapped(u8"Excluir o perfil \"%s\"? Os botões atuais continuam como estão.",
                       c.profile.c_str());
    ImGui::Spacing();
    float bw = S(110, scale);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - ImGui::GetStyle().ItemSpacing.x -
                         ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kDanger));
    if (ImGui::Button("Excluir", ImVec2(bw, 0))) {
      m.DeleteProfile(c.profile);
      c.profile.clear();
      m.Apply(c);
      g.notice.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(S(520, scale), 0));
  if (ImGui::BeginPopupModal(u8"Diagnóstico", nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    Muted(u8"Atualiza sozinho. Aperte botões no controle para ver chegando.");
    ImGui::Spacing();
    ImGui::PushFont(f.regular);
    ImGui::TextUnformatted(m.Diagnostics().c_str());
    ImGui::PopFont();
    ImGui::Spacing();
    if (ImGui::Button("Fechar", ImVec2(S(110, scale), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

}  // namespace

const unsigned short* GlyphRanges() {
  static const ImWchar ranges[] = {
      0x0020, 0x00FF,  // latim básico + acentos
      0x2190, 0x2193,  // setas
      0x2212, 0x2212,  // sinal de menos
      0x00B7, 0x00B7,  // ponto do meio
      0,
  };
  return ranges;
}

void ApplyTheme(float scale) {
  ImGuiStyle& st = ImGui::GetStyle();
  st = ImGuiStyle();
  ImGui::StyleColorsDark(&st);
  st.WindowPadding = ImVec2(20, 16);
  st.FramePadding = ImVec2(12, 7);
  st.ItemSpacing = ImVec2(10, 9);
  st.ItemInnerSpacing = ImVec2(8, 6);
  st.ScrollbarSize = 10;
  st.GrabMinSize = 14;
  st.WindowRounding = 10;
  st.ChildRounding = 12;
  st.FrameRounding = 7;
  st.PopupRounding = 10;
  st.ScrollbarRounding = 8;
  st.GrabRounding = 7;
  st.WindowBorderSize = 1;
  st.ChildBorderSize = 1;
  st.PopupBorderSize = 1;
  st.FrameBorderSize = 0;
  st.SelectableTextAlign = ImVec2(0, 0.5f);
  st.ScaleAllSizes(scale);

  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kMuted;
  c[ImGuiCol_WindowBg] = kBg;
  c[ImGuiCol_ChildBg] = kCard;
  c[ImGuiCol_PopupBg] = V(32, 35, 43);
  c[ImGuiCol_Border] = kCardBorder;
  c[ImGuiCol_FrameBg] = kControl;
  c[ImGuiCol_FrameBgHovered] = kControlHover;
  c[ImGuiCol_FrameBgActive] = kControlHover;
  c[ImGuiCol_Button] = kControl;
  c[ImGuiCol_ButtonHovered] = kControlHover;
  c[ImGuiCol_ButtonActive] = V(60, 66, 79);
  c[ImGuiCol_Header] = V(79, 140, 255, 60);
  c[ImGuiCol_HeaderHovered] = V(79, 140, 255, 90);
  c[ImGuiCol_HeaderActive] = V(79, 140, 255, 120);
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = kAccentHover;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_Separator] = kCardBorder;
  c[ImGuiCol_ScrollbarBg] = V(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = V(60, 66, 79);
  c[ImGuiCol_ModalWindowDimBg] = V(0, 0, 0, 140);
  c[ImGuiCol_NavCursor] = kAccent;
}

void DrawApp(UiModel& m, const Fonts& f, float scale) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##app", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(2);

  Header(m, f, scale);

  float footer = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2;
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float gap = ImGui::GetStyle().ItemSpacing.x * 1.5f;
  float left_w = std::floor((avail.x - gap) * 0.58f);
  ButtonsCard(m, f, scale, ImVec2(left_w, avail.y - footer));
  ImGui::SameLine(0, gap);
  BeginCard("##options", ImVec2(0, avail.y - footer));
  ProfileSection(m, f, scale);
  ImGui::Dummy(ImVec2(0, S(8, scale)));
  GyroSection(m, f);
  ImGui::Dummy(ImVec2(0, S(8, scale)));
  OptionsSection(m, f);
  EndCard();

  // Rodapé.
  ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y -
                       ImGui::GetFrameHeight());
  if (ImGui::Button(u8"Diagnóstico")) g.open_diag = true;
  ImGui::SameLine();
  if (ImGui::Button(u8"Pasta das configurações")) m.OpenFolder();
  const char* hide = "Fechar (continua na bandeja)";
  float wh = ImGui::CalcTextSize(hide).x + ImGui::GetStyle().FramePadding.x * 2;
  float we = ImGui::CalcTextSize("Sair").x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - wh - we -
                  ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::Button(hide)) m.HideWindow();
  ImGui::SameLine();
  if (ImGui::Button("Sair")) m.Exit();

  Modals(m, f, scale);
  ImGui::End();
}

}  // namespace ui
