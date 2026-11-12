#include "window.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "labels.h"
#include "pad_view.h"
#include "storage.h"

using namespace procon;

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr float kPcSlow = 900.0f, kPcNormal = 1600.0f, kPcFast = 2600.0f;

enum Id : int {
  IdStatus = 100,
  IdViewXbox,
  IdViewPc,
  IdPad,
  IdHoverText,
  IdResetDefault,
  IdLayoutName,
  IdProfileCombo,
  IdNewProfile,
  IdDeleteProfile,
  IdGyroCombo,
  IdSensLabel,
  IdSensBar,
  IdRecalibrate,
  IdRumble,
  IdHomeLight,
  IdSteamUsing,
  IdSteamAuto,
  IdPcMode,
  IdPcSpeed,
  IdAutostart,
  IdDiag,
  IdFolder,
  IdHide,
  IdExit,
  // Janela "Novo perfil".
  IdNewName = 200,
  IdNewStart,
};

const wchar_t kHintXbox[] =
    L"Clique num botão do desenho para escolher para qual botão do Xbox ele vai. Aperte um "
    L"botão no controle e ele acende aqui. Os contornos laranja são botões fora do padrão.";
const wchar_t kHintPc[] =
    L"Modo computador: o analógico direito move o mouse e o esquerdo rola a página. Clique "
    L"num botão do desenho para escolher o que ele faz. Estas teclas valem para todos os perfis.";

struct State {
  HINSTANCE inst = nullptr;
  HWND wnd = nullptr;
  HWND newp = nullptr;  // janela "Novo perfil"
  HWND pad = nullptr;
  AppHooks hooks;
  HFONT font = nullptr, bold = nullptr, big = nullptr;
  int dpi = 96;
  bool pc_view = false;
  bool updating = false;
  int tick = 0;
};
State g;

int S(int v) { return MulDiv(v, g.dpi, 96); }
HWND Child(int id) { return GetDlgItem(g.wnd, id); }

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
          int h, int id, HFONT font = nullptr) {
  HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst,
                           nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : g.font), TRUE);
  return c;
}

void SetCheck(int id, bool on) {
  SendMessageW(Child(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}
bool Checked(int id) { return SendMessageW(Child(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
int ComboSel(HWND combo) { return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)); }
void ComboAdd(HWND combo, const wchar_t* text) {
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

int GyroLevel(const Settings& c) {
  return c.gyro == GyroMode::RightStick ? StickLevelOf(c.stick_full_speed_dps)
                                        : MouseLevelOf(c.mouse_pixels_per_degree);
}

std::wstring MappingLabel(const Settings& c, int i) {
  return g.pc_view ? PcLabel(static_cast<int>(c.pc_buttons[i]))
                   : TargetLabel(static_cast<int>(c.buttons[i]));
}

void UpdateHover() {
  if (!g.wnd) return;
  int h = PadViewHover(g.pad);
  if (h < 0) {
    SetWindowTextW(Child(IdHoverText), g.pc_view ? kHintPc : kHintXbox);
    return;
  }
  Settings c = g.hooks.get();
  std::wstring text = std::wstring(SourceLabel(h)) + L"  \x2192  " + MappingLabel(c, h) +
                      L"\n(clique para trocar)";
  SetWindowTextW(Child(IdHoverText), text.c_str());
}

void UpdatePad(const Settings& c) {
  std::array<bool, kSources> changed{};
  ButtonMap def = DefaultPositionalMap();
  PcMap pdef = DefaultPcMap();
  for (int i = 0; i < kSources; ++i) {
    changed[i] = g.pc_view ? c.pc_buttons[i] != pdef[i] : c.buttons[i] != def[i];
  }
  EngineStatus st = g.hooks.status();
  PadViewSet(g.pad, st.connected ? st.buttons : 0, changed);
}

void FillProfiles(const Settings& c) {
  HWND combo = Child(IdProfileCombo);
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  ComboAdd(combo, L"(nenhum)");
  int sel = 0;
  auto names = ListProfiles();
  for (size_t i = 0; i < names.size(); ++i) {
    ComboAdd(combo, Widen(names[i]).c_str());
    if (names[i] == c.profile) sel = static_cast<int>(i) + 1;
  }
  SendMessageW(combo, CB_SETCURSEL, sel, 0);
  EnableWindow(Child(IdDeleteProfile), sel != 0);
}

void Refresh() {
  if (!g.wnd) return;
  g.updating = true;
  Settings c = g.hooks.get();
  FillProfiles(c);

  int gyro = c.gyro == GyroMode::Mouse ? 1 : c.gyro == GyroMode::RightStick ? 2 : 0;
  SendMessageW(Child(IdGyroCombo), CB_SETCURSEL, gyro, 0);
  int level = GyroLevel(c);
  SendMessageW(Child(IdSensBar), TBM_SETPOS, TRUE, level);
  std::wstring sens = L"Sensibilidade do giroscópio: " + std::to_wstring(level) + L" de 10";
  if (c.gyro == GyroMode::Off) sens = L"Sensibilidade do giroscópio (ligue o giroscópio)";
  SetWindowTextW(Child(IdSensLabel), sens.c_str());
  EnableWindow(Child(IdSensBar), c.gyro != GyroMode::Off);

  SetCheck(IdRumble, c.rumble);
  SetCheck(IdHomeLight, c.home_light);
  SetCheck(IdSteamUsing, c.steam == SteamMode::Always);
  SetCheck(IdSteamAuto, g.hooks.steam_base() == SteamMode::Auto);
  EnableWindow(Child(IdSteamAuto), c.steam != SteamMode::Always);
  SetCheck(IdPcMode, c.pc_mode);
  int speed = c.pc_mouse_speed < 1200 ? 0 : c.pc_mouse_speed < 2100 ? 1 : 2;
  SendMessageW(Child(IdPcSpeed), CB_SETCURSEL, speed, 0);
  SetCheck(IdAutostart, g.hooks.autostart());

  SetCheck(IdViewXbox, !g.pc_view);
  SetCheck(IdViewPc, g.pc_view);
  SetWindowTextW(Child(IdResetDefault),
                 g.pc_view ? L"Padrão do modo computador" : L"Padrão: pela posição");
  ShowWindow(Child(IdLayoutName), g.pc_view ? SW_HIDE : SW_SHOW);

  UpdatePad(c);
  UpdateHover();
  SetWindowTextW(Child(IdStatus), g.hooks.status_text().c_str());
  g.updating = false;
}

void Apply(const Settings& c) {
  g.hooks.apply(c);
  Refresh();
}

// --- perfis ---------------------------------------------------------------

std::string SelectedProfile() {
  HWND combo = Child(IdProfileCombo);
  int sel = ComboSel(combo);
  if (sel <= 0) return {};
  int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, sel, 0));
  std::wstring w(static_cast<size_t>(len) + 1, L'\0');
  SendMessageW(combo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(w.data()));
  w.resize(static_cast<size_t>(len));
  return Narrow(w);
}

void OnProfileSelected() {
  Settings c = g.hooks.get();
  std::string name = SelectedProfile();
  if (name.empty()) {
    c.profile.clear();
  } else {
    Settings p;
    if (!LoadProfile(name, p)) {
      MessageBoxW(g.wnd, L"Não consegui abrir esse perfil.", L"ProconWin", MB_ICONWARNING);
      Refresh();
      return;
    }
    ApplyProfile(p, c);
    c.profile = name;
  }
  Apply(c);
}

void OnDeleteProfile() {
  Settings c = g.hooks.get();
  if (c.profile.empty()) return;
  std::wstring q = L"Excluir o perfil \"" + Widen(c.profile) +
                   L"\"? Os botões atuais continuam como estão.";
  if (MessageBoxW(g.wnd, q.c_str(), L"ProconWin", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  DeleteProfile(c.profile);
  c.profile.clear();
  Apply(c);
}

void CloseNewProfile() {
  if (!g.newp) return;
  EnableWindow(g.wnd, TRUE);
  DestroyWindow(g.newp);
  g.newp = nullptr;
  SetForegroundWindow(g.wnd);
}

void OnCreateProfile() {
  wchar_t buf[128] = {};
  GetWindowTextW(GetDlgItem(g.newp, IdNewName), buf, 128);
  std::string name = SanitizeProfileName(Narrow(buf));
  if (name.empty()) {
    MessageBoxW(g.newp, L"Digite um nome para o perfil (por exemplo, o nome do jogo).",
                L"ProconWin", MB_ICONINFORMATION);
    return;
  }
  for (const auto& n : ListProfiles()) {
    if (_wcsicmp(Widen(n).c_str(), Widen(name).c_str()) == 0) {
      if (MessageBoxW(g.newp, L"Já existe um perfil com esse nome. Substituir?", L"ProconWin",
                      MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
      }
      name = n;
      break;
    }
  }
  Settings c = g.hooks.get();
  int start = ComboSel(GetDlgItem(g.newp, IdNewStart));
  if (start == 1) c.buttons = DefaultPositionalMap();
  if (start == 2) c.buttons = NintendoLabelMap();
  c.profile = name;
  if (!SaveProfile(name, c)) {
    MessageBoxW(g.newp, L"Não consegui salvar o perfil.", L"ProconWin", MB_ICONWARNING);
    return;
  }
  CloseNewProfile();
  g.pc_view = false;
  Apply(c);
  std::wstring done = L"Perfil \"" + Widen(name) +
                      L"\" criado e em uso. Agora clique nos botões do desenho para escolher "
                      L"a função de cada um: tudo fica salvo nele.";
  MessageBoxW(g.wnd, done.c_str(), L"ProconWin", MB_ICONINFORMATION);
}

LRESULT CALLBACK NewProfileProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND:
      if (LOWORD(wp) == IDOK) OnCreateProfile();
      else if (LOWORD(wp) == IDCANCEL) CloseNewProfile();
      return 0;
    case WM_CLOSE:
      CloseNewProfile();
      return 0;
  }
  return DefWindowProcW(wnd, msg, wp, lp);
}

void ShowNewProfile() {
  if (g.newp) {
    SetForegroundWindow(g.newp);
    return;
  }
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = NewProfileProc;
    wc.hInstance = g.inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"ProconWinNewProfile";
    RegisterClassW(&wc);
    registered = true;
  }
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  RECT r{0, 0, S(400), S(176)};
  AdjustWindowRect(&r, style, FALSE);
  RECT owner;
  GetWindowRect(g.wnd, &owner);
  int w = r.right - r.left, h = r.bottom - r.top;
  int x = owner.left + (owner.right - owner.left - w) / 2;
  int y = owner.top + (owner.bottom - owner.top - h) / 3;
  g.newp = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ProconWinNewProfile", L"Novo perfil", style, x,
                           y, w, h, g.wnd, nullptr, g.inst, nullptr);
  if (!g.newp) return;
  Make(g.newp, L"STATIC", L"Nome do perfil (por exemplo, o nome do jogo):", SS_LEFT, 16, 14, 368,
       20, -1);
  HWND edit = Make(g.newp, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 16, 36, 368,
                   24, IdNewName);
  SendMessageW(edit, EM_SETLIMITTEXT, 60, 0);
  Make(g.newp, L"STATIC", L"Começar com:", SS_LEFT, 16, 76, 100, 20, -1);
  HWND start = Make(g.newp, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 116, 72, 268, 200,
                    IdNewStart);
  ComboAdd(start, L"Os botões de agora");
  ComboAdd(start, L"Padrão: pela posição");
  ComboAdd(start, L"Padrão: pelo nome");
  SendMessageW(start, CB_SETCURSEL, 0, 0);
  Make(g.newp, L"BUTTON", L"Criar perfil", BS_DEFPUSHBUTTON | WS_TABSTOP, 176, 128, 110, 30, IDOK);
  Make(g.newp, L"BUTTON", L"Cancelar", BS_PUSHBUTTON | WS_TABSTOP, 294, 128, 90, 30, IDCANCEL);
  EnableWindow(g.wnd, FALSE);
  ShowWindow(g.newp, SW_SHOW);
  SetFocus(edit);
}

// --- botões do desenho ------------------------------------------------------

void OnPadClick() {
  int i = PadViewHover(g.pad);
  if (i < 0) return;
  Settings c = g.hooks.get();
  HMENU menu = CreatePopupMenu();
  int count = g.pc_view ? kPcActions : kTargets;
  int current = g.pc_view ? static_cast<int>(c.pc_buttons[i]) : static_cast<int>(c.buttons[i]);
  std::wstring title = std::wstring(SourceLabel(i)) + L" vai para:";
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, title.c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  for (int t = 0; t < count; ++t) {
    AppendMenuW(menu, MF_STRING | (t == current ? MF_CHECKED : 0), 1000 + t,
                g.pc_view ? PcLabel(t) : TargetLabel(t));
  }
  POINT pt;
  GetCursorPos(&pt);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.wnd, nullptr);
  DestroyMenu(menu);
  if (cmd < 1000) return;
  int t = cmd - 1000;
  if (g.pc_view) c.pc_buttons[i] = static_cast<PcAction>(t);
  else c.buttons[i] = static_cast<Target>(t);
  Apply(c);
}

// --- montagem -------------------------------------------------------------

void CreateControls() {
  HWND w = g.wnd;
  Make(w, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 16, 12, 790, 26, IdStatus, g.big);

  // Coluna esquerda: botões.
  Make(w, L"BUTTON", L"Botões nos jogos (Xbox)", BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, 16,
       48, 210, 24, IdViewXbox, g.bold);
  Make(w, L"BUTTON", L"Teclas do modo computador", BS_AUTORADIOBUTTON, 232, 48, 240, 24,
       IdViewPc, g.bold);
  g.pad = CreatePadView(w, IdPad, S(16), S(80), S(460), S(293));
  Make(w, L"STATIC", kHintXbox, SS_LEFT | SS_NOPREFIX, 16, 380, 460, 50, IdHoverText);
  Make(w, L"BUTTON", L"Padrão: pela posição", BS_PUSHBUTTON | WS_TABSTOP, 16, 436, 190, 28,
       IdResetDefault);
  Make(w, L"BUTTON", L"Padrão: pelo nome", BS_PUSHBUTTON | WS_TABSTOP, 214, 436, 160, 28,
       IdLayoutName);

  // Coluna direita: perfil e opções.
  const int x = 500, cw = 304;
  Make(w, L"STATIC", L"Perfil em uso", SS_LEFT, x, 50, cw, 20, -1, g.bold);
  Make(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, 72, cw, 300,
       IdProfileCombo);
  Make(w, L"BUTTON", L"Novo perfil...", BS_PUSHBUTTON | WS_TABSTOP, x, 104, 150, 28,
       IdNewProfile);
  Make(w, L"BUTTON", L"Excluir perfil", BS_PUSHBUTTON | WS_TABSTOP, x + 158, 104, 146, 28,
       IdDeleteProfile);

  Make(w, L"STATIC", L"Giroscópio", SS_LEFT, x, 146, cw, 20, -1, g.bold);
  HWND gyro = Make(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, x, 168, cw, 200,
                   IdGyroCombo);
  ComboAdd(gyro, L"Desligado");
  ComboAdd(gyro, L"Mouse (jogos de tiro)");
  ComboAdd(gyro, L"Analógico direito");
  Make(w, L"STATIC", L"", SS_LEFT, x, 202, cw, 20, IdSensLabel);
  HWND bar = Make(w, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, x - 4, 222,
                  cw + 8, 32, IdSensBar);
  SendMessageW(bar, TBM_SETRANGE, TRUE, MAKELPARAM(1, kGyroLevels));
  SendMessageW(bar, TBM_SETPAGESIZE, 0, 1);
  Make(w, L"BUTTON", L"Recalibrar (controle parado)", BS_PUSHBUTTON | WS_TABSTOP, x, 258, 220,
       28, IdRecalibrate);

  Make(w, L"STATIC", L"Opções", SS_LEFT, x, 298, cw, 20, -1, g.bold);
  Make(w, L"BUTTON", L"Vibração", BS_AUTOCHECKBOX | WS_TABSTOP, x, 320, cw, 22, IdRumble);
  Make(w, L"BUTTON", L"Luz do botão HOME", BS_AUTOCHECKBOX | WS_TABSTOP, x, 344, cw, 22,
       IdHomeLight);
  Make(w, L"BUTTON", L"Estou usando a Steam (pausa o ProconWin)", BS_AUTOCHECKBOX | WS_TABSTOP,
       x, 368, cw, 22, IdSteamUsing);
  Make(w, L"BUTTON", L"Pausar sozinho com jogos da Steam", BS_AUTOCHECKBOX | WS_TABSTOP, x, 392,
       cw, 22, IdSteamAuto);
  Make(w, L"BUTTON", L"Modo computador (ou segure Captura 1 s)", BS_AUTOCHECKBOX | WS_TABSTOP, x,
       416, cw, 22, IdPcMode);
  Make(w, L"STATIC", L"Mouse do modo computador:", SS_LEFT, x + 18, 444, 160, 20, -1);
  HWND speed = Make(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, x + 180, 440, cw - 180,
                    200, IdPcSpeed);
  ComboAdd(speed, L"Lento");
  ComboAdd(speed, L"Normal");
  ComboAdd(speed, L"Rápido");
  Make(w, L"BUTTON", L"Iniciar com o Windows (na bandeja)", BS_AUTOCHECKBOX | WS_TABSTOP, x, 470,
       cw, 22, IdAutostart);

  // Rodapé.
  Make(w, L"BUTTON", L"Diagnóstico", BS_PUSHBUTTON | WS_TABSTOP, 16, 508, 110, 28, IdDiag);
  Make(w, L"BUTTON", L"Pasta das configurações", BS_PUSHBUTTON | WS_TABSTOP, 134, 508, 180, 28,
       IdFolder);
  Make(w, L"BUTTON", L"Fechar (fica na bandeja)", BS_PUSHBUTTON | WS_TABSTOP, 500, 508, 196, 28,
       IdHide);
  Make(w, L"BUTTON", L"Sair", BS_PUSHBUTTON | WS_TABSTOP, 704, 508, 100, 28, IdExit);
}

void OnCommand(int id, int code) {
  if (g.updating) return;
  Settings c = g.hooks.get();
  switch (id) {
    case IdViewXbox:
    case IdViewPc:
      if (code == BN_CLICKED) {
        g.pc_view = id == IdViewPc;
        Refresh();
      }
      return;
    case IdPad:
      if (code == kPadClick) OnPadClick();
      else if (code == kPadHover) UpdateHover();
      return;
    case IdResetDefault:
      if (g.pc_view) c.pc_buttons = DefaultPcMap();
      else c.buttons = DefaultPositionalMap();
      Apply(c);
      return;
    case IdLayoutName:
      c.buttons = NintendoLabelMap();
      Apply(c);
      return;
    case IdProfileCombo:
      if (code == CBN_SELCHANGE) OnProfileSelected();
      return;
    case IdNewProfile: ShowNewProfile(); return;
    case IdDeleteProfile: OnDeleteProfile(); return;
    case IdGyroCombo:
      if (code != CBN_SELCHANGE) return;
      switch (ComboSel(Child(IdGyroCombo))) {
        case 1: c.gyro = GyroMode::Mouse; break;
        case 2: c.gyro = GyroMode::RightStick; break;
        default: c.gyro = GyroMode::Off; break;
      }
      Apply(c);
      return;
    case IdRecalibrate: g.hooks.recalibrate(); return;
    case IdRumble: c.rumble = Checked(IdRumble); Apply(c); return;
    case IdHomeLight: c.home_light = Checked(IdHomeLight); Apply(c); return;
    case IdSteamUsing:
      c.steam = Checked(IdSteamUsing) ? SteamMode::Always : g.hooks.steam_base();
      Apply(c);
      return;
    case IdSteamAuto: {
      SteamMode base = Checked(IdSteamAuto) ? SteamMode::Auto : SteamMode::Never;
      g.hooks.set_steam_base(base);
      if (c.steam != SteamMode::Always) c.steam = base;
      Apply(c);
      return;
    }
    case IdPcMode: c.pc_mode = Checked(IdPcMode); Apply(c); return;
    case IdPcSpeed: {
      if (code != CBN_SELCHANGE) return;
      int s = ComboSel(Child(IdPcSpeed));
      c.pc_mouse_speed = s == 0 ? kPcSlow : s == 2 ? kPcFast : kPcNormal;
      Apply(c);
      return;
    }
    case IdAutostart: g.hooks.set_autostart(Checked(IdAutostart)); Refresh(); return;
    case IdDiag: g.hooks.diagnostics(); return;
    case IdFolder: g.hooks.open_folder(); return;
    case IdHide:
    case IDCANCEL:
      ShowWindow(g.wnd, SW_HIDE);
      return;
    case IdExit: g.hooks.exit_app(); return;
  }
}

void OnSensScroll() {
  if (g.updating) return;
  Settings c = g.hooks.get();
  if (c.gyro == GyroMode::Off) return;
  int level = static_cast<int>(SendMessageW(Child(IdSensBar), TBM_GETPOS, 0, 0));
  if (level == GyroLevel(c)) return;
  if (c.gyro == GyroMode::RightStick) c.stick_full_speed_dps = StickSpeedForLevel(level);
  else c.mouse_pixels_per_degree = MouseSensForLevel(level);
  Apply(c);
}

LRESULT CALLBACK MainProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND:
      OnCommand(LOWORD(wp), HIWORD(wp));
      return 0;
    case WM_HSCROLL:
      if (reinterpret_cast<HWND>(lp) == Child(IdSensBar)) OnSensScroll();
      return 0;
    case WM_TIMER:
      UpdatePad(g.hooks.get());
      if (++g.tick % 10 == 0) SetWindowTextW(Child(IdStatus), g.hooks.status_text().c_str());
      return 0;
    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wp);
      SetBkMode(dc, TRANSPARENT);
      if (reinterpret_cast<HWND>(lp) == Child(IdStatus)) SetTextColor(dc, RGB(0, 90, 170));
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_SHOWWINDOW:
      // Escondida, não precisa atualizar o desenho 20 vezes por segundo.
      if (wp) {
        Refresh();
        SetTimer(wnd, kTimerId, 50, nullptr);
      } else {
        KillTimer(wnd, kTimerId);
      }
      return 0;
    case WM_CLOSE:
      ShowWindow(wnd, SW_HIDE);
      return 0;
  }
  return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

void ShowMainWindow(HINSTANCE inst, const AppHooks& hooks) {
  g.hooks = hooks;
  if (g.wnd) {
    ShowWindow(g.wnd, SW_SHOW);
    if (IsIconic(g.wnd)) ShowWindow(g.wnd, SW_RESTORE);
    SetForegroundWindow(g.wnd);
    return;
  }
  g.inst = inst;

  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  WNDCLASSW wc{};
  wc.lpfnWndProc = MainProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"ProconWinMain";
  RegisterClassW(&wc);

  HDC screen = GetDC(nullptr);
  g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
  ReleaseDC(nullptr, screen);

  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  g.font = CreateFontIndirectW(&ncm.lfMessageFont);
  LOGFONTW lf = ncm.lfMessageFont;
  lf.lfWeight = FW_BOLD;
  g.bold = CreateFontIndirectW(&lf);
  lf.lfHeight = lf.lfHeight * 13 / 10;
  g.big = CreateFontIndirectW(&lf);

  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT r{0, 0, S(820), S(548)};
  AdjustWindowRect(&r, style, FALSE);
  g.wnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"ProconWinMain", L"ProconWin", style,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                          nullptr, nullptr, inst, nullptr);
  if (!g.wnd) return;
  CreateControls();
  ShowWindow(g.wnd, SW_SHOW);
  SetForegroundWindow(g.wnd);
}

void RefreshMainWindow() {
  if (g.wnd && IsWindowVisible(g.wnd)) Refresh();
}

bool HandleWindowMessage(MSG* msg) {
  if (g.newp && IsDialogMessageW(g.newp, msg)) return true;
  if (g.wnd && IsDialogMessageW(g.wnd, msg)) return true;
  return false;
}
