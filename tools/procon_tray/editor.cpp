#include "editor.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "storage.h"

using namespace procon;

namespace {

constexpr int kSources = static_cast<int>(Source::Count);
constexpr int kTargets = static_cast<int>(Target::Count);
constexpr int kPcActions = static_cast<int>(PcAction::Count);
constexpr UINT_PTR kTimerId = 1;

enum CtrlId : int {
  IdProfileCombo = 1000,
  IdDeleteProfile,
  IdNewName,
  IdCreateProfile,
  IdPresetPosition,
  IdPresetName,
  IdClose,
  IdViewXbox,
  IdViewPc,
  IdHelp,
  IdPcOn,
  IdLabelBase = 2000,   // + índice do botão
  IdComboBase = 3000,   // + índice do botão
};

const wchar_t* SourceLabel(int i) {
  static const wchar_t* names[] = {
      L"A", L"B", L"X", L"Y", L"L", L"R", L"ZL", L"ZR",
      L"\x2212 (menos)", L"+ (mais)", L"L3 (clique esq.)", L"R3 (clique dir.)", L"HOME",
      L"Captura", L"Direcional \x2191", L"Direcional \x2193", L"Direcional \x2190",
      L"Direcional \x2192"};
  static_assert(sizeof(names) / sizeof(names[0]) == kSources, "names");
  return names[i];
}

const wchar_t* TargetLabel(int i) {
  static const wchar_t* names[] = {
      L"(nada)", L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT (gatilho esq.)",
      L"RT (gatilho dir.)", L"Back / View", L"Start / Menu", L"LS (clique analógico esq.)",
      L"RS (clique analógico dir.)", L"Guide (botão Xbox)", L"D-pad \x2191", L"D-pad \x2193",
      L"D-pad \x2190", L"D-pad \x2192"};
  static_assert(sizeof(names) / sizeof(names[0]) == kTargets, "names");
  return names[i];
}

const wchar_t* PcLabel(int i) {
  static const wchar_t* names[] = {
      L"(nada)", L"Clique esquerdo", L"Clique direito", L"Clique do meio", L"Enter", L"Esc",
      L"Espaço", L"Apagar (Backspace)", L"Tab", L"Delete", L"Seta \x2191", L"Seta \x2193",
      L"Seta \x2190", L"Seta \x2192", L"Page Up", L"Page Down", L"Tecla Windows (Iniciar)",
      L"Alt+Tab (trocar janela)", L"Ver todas as janelas", L"Fechar janela (Alt+F4)",
      L"Teclado virtual", L"Recortar a tela", L"Volume +", L"Volume \x2212", L"Mudo",
      L"Play / pausa", L"Voltar (navegador)", L"Avançar (navegador)"};
  static_assert(sizeof(names) / sizeof(names[0]) == kPcActions, "names");
  return names[i];
}

const wchar_t kHelpXbox[] =
    L"Escolha para qual botão do Xbox vai cada botão do Pro Controller. A mudança vale na "
    L"hora (e fica salva no perfil em uso). Aperte um botão no controle para ver qual é: ele "
    L"fica destacado.";
const wchar_t kHelpPc[] =
    L"Modo computador: o analógico direito move o mouse e o esquerdo rola a página. Escolha "
    L"o que cada botão faz. Estas teclas valem para todos os perfis. Segure Captura por 1 s "
    L"no controle para ligar ou desligar.";

struct State {
  HWND wnd = nullptr;
  EditorCallbacks cb;
  HFONT font = nullptr;
  HFONT bold = nullptr;
  int dpi = 96;
  uint32_t pressed = 0;
  bool updating = false;  // preenchendo os controles (ignora CBN_SELCHANGE)
  bool pc_view = false;   // editando as teclas do modo computador
  int filled_view = -1;   // qual lista está nas caixas agora
};
State g;

int S(int v) { return MulDiv(v, g.dpi, 96); }

HWND Child(int id) { return GetDlgItem(g.wnd, id); }

void Apply(const Settings& s) {
  g.cb.apply(s);
  // Com um perfil em uso, toda mudança vai direto para ele.
  if (!s.profile.empty()) SaveProfile(s.profile, s);
}

void FillProfiles(const Settings& cfg) {
  HWND combo = Child(IdProfileCombo);
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(nenhum)"));
  int sel = 0;
  auto names = ListProfiles();
  for (size_t i = 0; i < names.size(); ++i) {
    std::wstring w = Widen(names[i]);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
    if (names[i] == cfg.profile) sel = static_cast<int>(i) + 1;
  }
  SendMessageW(combo, CB_SETCURSEL, sel, 0);
  EnableWindow(Child(IdDeleteProfile), sel != 0);
}

void FillCombos() {
  int view = g.pc_view ? 1 : 0;
  if (g.filled_view == view) return;
  g.filled_view = view;
  for (int i = 0; i < kSources; ++i) {
    HWND combo = Child(IdComboBase + i);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int n = g.pc_view ? kPcActions : kTargets;
    for (int t = 0; t < n; ++t) {
      SendMessageW(combo, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(g.pc_view ? PcLabel(t) : TargetLabel(t)));
    }
  }
  SetWindowTextW(Child(IdHelp), g.pc_view ? kHelpPc : kHelpXbox);
  SetWindowTextW(Child(IdPresetPosition),
                 g.pc_view ? L"Padrão do modo computador" : L"Padrão: pela posição");
  ShowWindow(Child(IdPresetName), g.pc_view ? SW_HIDE : SW_SHOW);
  CheckRadioButton(g.wnd, IdViewXbox, IdViewPc, g.pc_view ? IdViewPc : IdViewXbox);
}

void Refresh() {
  if (!g.wnd) return;
  g.updating = true;
  Settings cfg = g.cb.get();
  FillProfiles(cfg);
  FillCombos();
  for (int i = 0; i < kSources; ++i) {
    WPARAM sel = g.pc_view ? static_cast<WPARAM>(cfg.pc_buttons[i])
                           : static_cast<WPARAM>(cfg.buttons[i]);
    SendMessageW(Child(IdComboBase + i), CB_SETCURSEL, sel, 0);
  }
  CheckDlgButton(g.wnd, IdPcOn, cfg.pc_mode ? BST_CHECKED : BST_UNCHECKED);
  g.updating = false;
}

std::string SelectedProfile() {
  HWND combo = Child(IdProfileCombo);
  int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (sel <= 0) return {};
  int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, sel, 0));
  std::wstring w(static_cast<size_t>(len) + 1, L'\0');
  SendMessageW(combo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(w.data()));
  w.resize(static_cast<size_t>(len));
  return Narrow(w);
}

void OnProfileSelected() {
  Settings cfg = g.cb.get();
  std::string name = SelectedProfile();
  if (name.empty()) {
    cfg.profile.clear();
  } else {
    Settings p;
    if (!LoadProfile(name, p)) {
      MessageBoxW(g.wnd, L"Não consegui abrir esse perfil.", L"ProconWin", MB_ICONWARNING);
      Refresh();
      return;
    }
    ApplyProfile(p, cfg);
    cfg.profile = name;
  }
  g.cb.apply(cfg);
  Refresh();
}

void OnCreateProfile() {
  wchar_t buf[128] = {};
  GetWindowTextW(Child(IdNewName), buf, 128);
  std::string name = SanitizeProfileName(Narrow(buf));
  if (name.empty()) {
    MessageBoxW(g.wnd, L"Digite um nome para o perfil (por exemplo, o nome do jogo).",
                L"ProconWin", MB_ICONINFORMATION);
    return;
  }
  auto names = ListProfiles();
  for (const auto& n : names) {
    if (_wcsicmp(Widen(n).c_str(), Widen(name).c_str()) == 0) {
      if (MessageBoxW(g.wnd, L"Já existe um perfil com esse nome. Substituir?", L"ProconWin",
                      MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
      }
      name = n;
      break;
    }
  }
  Settings cfg = g.cb.get();
  cfg.profile = name;
  if (!SaveProfile(name, cfg)) {
    MessageBoxW(g.wnd, L"Não consegui salvar o perfil.", L"ProconWin", MB_ICONWARNING);
    return;
  }
  g.cb.apply(cfg);
  SetWindowTextW(Child(IdNewName), L"");
  Refresh();
}

void OnDeleteProfile() {
  Settings cfg = g.cb.get();
  if (cfg.profile.empty()) return;
  std::wstring q = L"Excluir o perfil \"" + Widen(cfg.profile) +
                   L"\"? Os botões atuais continuam como estão.";
  if (MessageBoxW(g.wnd, q.c_str(), L"ProconWin", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  DeleteProfile(cfg.profile);
  cfg.profile.clear();
  g.cb.apply(cfg);
  Refresh();
}

void OnPreset(const ButtonMap& map) {
  Settings cfg = g.cb.get();
  cfg.buttons = map;
  Apply(cfg);
  Refresh();
}

void OnButtonChanged(int i) {
  int sel = static_cast<int>(SendMessageW(Child(IdComboBase + i), CB_GETCURSEL, 0, 0));
  if (sel < 0 || sel >= (g.pc_view ? kPcActions : kTargets)) return;
  Settings cfg = g.cb.get();
  if (g.pc_view) {
    cfg.pc_buttons[i] = static_cast<PcAction>(sel);
    g.cb.apply(cfg);  // global, não vai para o perfil
  } else {
    cfg.buttons[i] = static_cast<Target>(sel);
    Apply(cfg);
  }
}

void OnPcPreset() {
  Settings cfg = g.cb.get();
  cfg.pc_buttons = DefaultPcMap();
  g.cb.apply(cfg);
  Refresh();
}

void OnPcToggle() {
  Settings cfg = g.cb.get();
  cfg.pc_mode = IsDlgButtonChecked(g.wnd, IdPcOn) == BST_CHECKED;
  g.cb.apply(cfg);
}

HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
          int id) {
  HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                           g.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g.wnd, GWLP_HINSTANCE)),
                           nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return c;
}

void CreateControls() {
  const int m = 12;  // margem
  int y = m;
  Make(L"STATIC", L"Perfil em uso:", SS_LEFT, m, y + 4, 90, 20, -1);
  Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, m + 95, y, 220, 300,
       IdProfileCombo);
  Make(L"BUTTON", L"Excluir perfil", BS_PUSHBUTTON | WS_TABSTOP, m + 325, y - 1, 110, 26,
       IdDeleteProfile);
  y += 34;
  Make(L"STATIC", L"Novo perfil:", SS_LEFT, m, y + 4, 90, 20, -1);
  HWND edit = Make(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, m + 95, y, 220, 24,
                   IdNewName);
  SendMessageW(edit, EM_SETLIMITTEXT, 60, 0);
  SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"nome do jogo"));
  Make(L"BUTTON", L"Criar com os botões atuais", BS_PUSHBUTTON | WS_TABSTOP, m + 325, y - 1, 200,
       26, IdCreateProfile);
  y += 40;
  Make(L"STATIC", L"Editar:", SS_LEFT, m, y + 4, 90, 20, -1);
  Make(L"BUTTON", L"Jogos (controle de Xbox)", BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, m + 95,
       y, 200, 24, IdViewXbox);
  Make(L"BUTTON", L"Modo computador", BS_AUTORADIOBUTTON, m + 300, y, 160, 24, IdViewPc);
  y += 32;
  Make(L"STATIC", kHelpXbox, SS_LEFT, m, y, 560, 50, IdHelp);
  y += 58;

  // Duas colunas de 9 botões.
  const int col_w = 290;
  const int rows = (kSources + 1) / 2;
  for (int i = 0; i < kSources; ++i) {
    int col = i / rows, row = i % rows;
    int x = m + col * col_w;
    int yy = y + row * 32;
    Make(L"STATIC", SourceLabel(i), SS_LEFT | SS_NOTIFY, x, yy + 4, 110, 20, IdLabelBase + i);
    Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 112, yy, 160, 400,
         IdComboBase + i);
  }
  y += rows * 32 + 8;
  Make(L"STATIC",
       L"Captura também recalibra o giroscópio, segurar Captura por 1 s liga o modo computador "
       L"e segurar HOME por 3 s desliga o controle.",
       SS_LEFT, m, y, 560, 36, -1);
  y += 44;
  Make(L"BUTTON", L"Padrão: pela posição", BS_PUSHBUTTON | WS_TABSTOP, m, y, 190, 28,
       IdPresetPosition);
  Make(L"BUTTON", L"Padrão: pelo nome", BS_PUSHBUTTON | WS_TABSTOP, m + 200, y, 150, 28,
       IdPresetName);
  Make(L"BUTTON", L"Modo computador ligado", BS_AUTOCHECKBOX | WS_TABSTOP, m + 200, y + 36, 250,
       24, IdPcOn);
  Make(L"BUTTON", L"Fechar", BS_DEFPUSHBUTTON | WS_TABSTOP, m + 480, y, 90, 28, IdClose);
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      if (id == IdClose || id == IDCANCEL) {
        DestroyWindow(hwnd);
      } else if (id == IdProfileCombo && code == CBN_SELCHANGE && !g.updating) {
        OnProfileSelected();
      } else if (id == IdCreateProfile || (id == IDOK && GetFocus() == Child(IdNewName))) {
        OnCreateProfile();
      } else if (id == IdDeleteProfile) {
        OnDeleteProfile();
      } else if (id == IdPresetPosition) {
        if (g.pc_view) OnPcPreset();
        else OnPreset(DefaultPositionalMap());
      } else if ((id == IdViewXbox || id == IdViewPc) && code == BN_CLICKED) {
        g.pc_view = id == IdViewPc;
        Refresh();
      } else if (id == IdPcOn && code == BN_CLICKED && !g.updating) {
        OnPcToggle();
      } else if (id == IdPresetName) {
        OnPreset(NintendoLabelMap());
      } else if (id >= IdComboBase && id < IdComboBase + kSources && code == CBN_SELCHANGE &&
                 !g.updating) {
        OnButtonChanged(id - IdComboBase);
      }
      return 0;
    }
    case WM_TIMER: {
      uint32_t now = g.cb.pressed_buttons ? g.cb.pressed_buttons() : 0;
      if (now != g.pressed) {
        uint32_t changed = now ^ g.pressed;
        g.pressed = now;
        for (int i = 0; i < kSources; ++i) {
          if (changed & SourceBit(static_cast<Source>(i))) {
            HWND label = Child(IdLabelBase + i);
            SendMessageW(label, WM_SETFONT,
                         reinterpret_cast<WPARAM>((now & SourceBit(static_cast<Source>(i)))
                                                      ? g.bold
                                                      : g.font),
                         FALSE);
            InvalidateRect(label, nullptr, TRUE);
          }
        }
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      HWND c = reinterpret_cast<HWND>(lp);
      int id = GetDlgCtrlID(c);
      HDC dc = reinterpret_cast<HDC>(wp);
      SetBkMode(dc, TRANSPARENT);
      if (id >= IdLabelBase && id < IdLabelBase + kSources &&
          (g.pressed & SourceBit(static_cast<Source>(id - IdLabelBase)))) {
        SetTextColor(dc, RGB(0, 102, 204));
      }
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kTimerId);
      if (g.font) DeleteObject(g.font);
      if (g.bold) DeleteObject(g.bold);
      g.font = g.bold = nullptr;
      g.wnd = nullptr;
      g.pressed = 0;
      g.filled_view = -1;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND ButtonEditorWindow() { return g.wnd; }

void RefreshButtonEditor() { Refresh(); }

void ShowButtonEditor(HINSTANCE inst, const EditorCallbacks& cb, bool pc_view) {
  g.pc_view = pc_view;
  if (g.wnd) {
    Refresh();
    ShowWindow(g.wnd, SW_RESTORE);
    SetForegroundWindow(g.wnd);
    return;
  }
  g.cb = cb;

  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = EditorProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"ProconWinEditor";
    RegisterClassW(&wc);
    registered = true;
  }

  HDC screen = GetDC(nullptr);
  g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
  ReleaseDC(nullptr, screen);

  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  g.font = CreateFontIndirectW(&ncm.lfMessageFont);
  LOGFONTW bold = ncm.lfMessageFont;
  bold.lfWeight = FW_BOLD;
  g.bold = CreateFontIndirectW(&bold);

  RECT r{0, 0, S(594), S(592)};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  g.wnd = CreateWindowExW(0, L"ProconWinEditor", L"ProconWin - botões e perfis", style,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                          nullptr, nullptr, inst, nullptr);
  if (!g.wnd) return;
  CreateControls();
  Refresh();
  SetTimer(g.wnd, kTimerId, 50, nullptr);
  ShowWindow(g.wnd, SW_SHOW);
  SetForegroundWindow(g.wnd);
}
