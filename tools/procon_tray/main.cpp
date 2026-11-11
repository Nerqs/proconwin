// procon_tray: o ProconWin como app na bandeja do Windows (ao lado do
// relógio). Clique com o botão direito no ícone para ver o estado do
// controle e mudar as opções. As escolhas ficam salvas em
// %APPDATA%\ProconWin\config.ini.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "procon/settings.h"
#include "storage.h"

using namespace procon;

namespace {

constexpr UINT kTrayMsg = WM_APP + 1;   // eventos do ícone
constexpr UINT kEngineMsg = WM_APP + 2; // eventos do motor (lParam = EventMsg*)
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kIconId = 1;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"ProconWin";

enum MenuId : UINT {
  IdGyroOff = 100,
  IdGyroMouse,
  IdGyroStick,
  IdLayoutPosition,
  IdLayoutName,
  IdSteamUsing,
  IdSteamAuto,
  IdHomeLight,
  IdRecalibrate,
  IdAutostart,
  IdOpenConfig,
  IdEditButtons,
  IdNoProfile,
  IdExit,
  IdProfileBase = 500,  // + índice em g_profiles
};

struct EventMsg {
  EngineEvent event;
  std::string text;
};

HWND g_wnd = nullptr;
UINT g_taskbar_created = 0;
Engine g_engine;
Settings g_cfg;
SteamMode g_steam_base = SteamMode::Auto;  // modo quando "Estou usando a Steam" está desligado
bool g_low_battery_warned = false;
HINSTANCE g_inst = nullptr;
std::vector<std::string> g_profiles;  // nomes mostrados no último menu

bool AutostartEnabled() {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  bool on = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
  RegCloseKey(key);
  return on;
}

void SetAutostart(bool on) {
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  if (on) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                   static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValue);
  }
  RegCloseKey(key);
}

std::wstring StatusText() {
  EngineStatus st = g_engine.GetStatus();
  if (!st.driver_error.empty()) return L"Driver do controle virtual não encontrado";
  if (st.paused) {
    if (st.steam_game.empty()) return L"Pausado: a Steam está com o controle";
    return L"Pausado: " + Widen(st.steam_game) + L" (Steam)";
  }
  if (!st.connected) return L"Esperando o Pro Controller";
  std::wstring s = st.usb ? L"Conectado (USB)" : L"Conectado (Bluetooth)";
  if (!st.usb || st.charging) {
    s += L" - bateria " + std::to_wstring(st.battery * 100 / 8) + L"%";
    if (st.charging) s += L", carregando";
  }
  return s;
}

void AddTrayIcon() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = g_wnd;
  nid.uID = kIconId;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = kTrayMsg;
  nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
  wcscpy_s(nid.szTip, L"ProconWin");
  Shell_NotifyIconW(NIM_ADD, &nid);
}

void UpdateTip() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = g_wnd;
  nid.uID = kIconId;
  nid.uFlags = NIF_TIP;
  std::wstring tip = L"ProconWin\n" + StatusText();
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void Notify(const std::wstring& text, bool warning = false) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = g_wnd;
  nid.uID = kIconId;
  nid.uFlags = NIF_INFO;
  nid.dwInfoFlags = warning ? NIIF_WARNING : NIIF_INFO;
  wcscpy_s(nid.szInfoTitle, L"ProconWin");
  wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void RemoveTrayIcon() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = g_wnd;
  nid.uID = kIconId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ApplySettings() {
  g_engine.SetSettings(g_cfg);
  SaveSettings(g_cfg);
  // Com um perfil em uso, botões e giroscópio vão para ele também.
  if (!g_cfg.profile.empty()) SaveProfile(g_cfg.profile, g_cfg);
  RefreshButtonEditor();
}

void OpenEditor() {
  EditorCallbacks cb;
  cb.get = [] { return g_cfg; };
  cb.apply = [](const Settings& s) {
    g_cfg = s;
    g_engine.SetSettings(g_cfg);
    SaveSettings(g_cfg);
  };
  cb.pressed_buttons = [] {
    EngineStatus st = g_engine.GetStatus();
    return st.connected ? st.buttons : 0u;
  };
  ShowButtonEditor(g_inst, cb);
}

void SelectProfile(const std::string& name) {
  if (name.empty()) {
    g_cfg.profile.clear();
    return;
  }
  Settings p;
  if (!LoadProfile(name, p)) {
    Notify(L"Não consegui abrir o perfil " + Widen(name) + L".", true);
    return;
  }
  ApplyProfile(p, g_cfg);
  g_cfg.profile = name;
}

void ShowMenu() {
  HMENU gyro = CreatePopupMenu();
  AppendMenuW(gyro, MF_STRING, IdGyroOff, L"Desligado");
  AppendMenuW(gyro, MF_STRING, IdGyroMouse, L"Mouse (jogos de tiro)");
  AppendMenuW(gyro, MF_STRING, IdGyroStick, L"Analógico direito");
  UINT gyro_sel = g_cfg.gyro == GyroMode::Mouse        ? IdGyroMouse
                  : g_cfg.gyro == GyroMode::RightStick ? IdGyroStick
                                                       : IdGyroOff;
  CheckMenuRadioItem(gyro, IdGyroOff, IdGyroStick, gyro_sel, MF_BYCOMMAND);
  AppendMenuW(gyro, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(gyro, MF_STRING, IdRecalibrate, L"Recalibrar (deixe o controle parado)");

  HMENU layout = CreatePopupMenu();
  bool by_position = g_cfg.buttons == DefaultPositionalMap();
  bool by_name = g_cfg.buttons == NintendoLabelMap();
  AppendMenuW(layout, MF_STRING | (by_position ? MF_CHECKED : 0), IdLayoutPosition,
              L"Pela posição (B da Nintendo = A do Xbox)");
  AppendMenuW(layout, MF_STRING | (by_name ? MF_CHECKED : 0), IdLayoutName,
              L"Pelo nome (A da Nintendo = A do Xbox)");
  if (!by_position && !by_name) {
    AppendMenuW(layout, MF_STRING | MF_GRAYED | MF_CHECKED, 0, L"Personalizado");
  }
  AppendMenuW(layout, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(layout, MF_STRING, IdEditButtons, L"Editar botões e perfis...");

  HMENU profiles = CreatePopupMenu();
  g_profiles = ListProfiles();
  AppendMenuW(profiles, MF_STRING | (g_cfg.profile.empty() ? MF_CHECKED : 0), IdNoProfile,
              L"(nenhum)");
  for (size_t i = 0; i < g_profiles.size() && i < 200; ++i) {
    AppendMenuW(profiles, MF_STRING | (g_profiles[i] == g_cfg.profile ? MF_CHECKED : 0),
                IdProfileBase + static_cast<UINT>(i), Widen(g_profiles[i]).c_str());
  }
  AppendMenuW(profiles, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(profiles, MF_STRING, IdEditButtons, L"Criar ou excluir perfis...");

  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, StatusText().c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gyro), L"Giroscópio");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layout), L"Botões");
  std::wstring profile_label =
      g_cfg.profile.empty() ? L"Perfil" : L"Perfil: " + Widen(g_cfg.profile);
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles), profile_label.c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (g_cfg.steam == SteamMode::Always ? MF_CHECKED : 0), IdSteamUsing,
              L"Estou usando a Steam");
  AppendMenuW(menu,
              MF_STRING | (g_steam_base == SteamMode::Auto ? MF_CHECKED : 0) |
                  (g_cfg.steam == SteamMode::Always ? MF_GRAYED : 0),
              IdSteamAuto, L"Pausar sozinho com jogos da Steam");
  AppendMenuW(menu, MF_STRING | (g_cfg.home_light ? MF_CHECKED : 0), IdHomeLight,
              L"Luz do botão HOME");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), IdAutostart,
              L"Iniciar com o Windows");
  AppendMenuW(menu, MF_STRING, IdOpenConfig, L"Abrir a pasta das configurações");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdExit, L"Sair");

  POINT pt;
  GetCursorPos(&pt);
  // Sem isso o menu não fecha ao clicar fora dele.
  SetForegroundWindow(g_wnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, nullptr);
  PostMessageW(g_wnd, WM_NULL, 0, 0);
  DestroyMenu(menu);  // destrói os submenus junto
}

void OnCommand(UINT id) {
  if (id >= IdProfileBase && id < IdProfileBase + g_profiles.size()) {
    SelectProfile(g_profiles[id - IdProfileBase]);
    ApplySettings();
    return;
  }
  switch (id) {
    case IdGyroOff: g_cfg.gyro = GyroMode::Off; break;
    case IdGyroMouse: g_cfg.gyro = GyroMode::Mouse; break;
    case IdGyroStick: g_cfg.gyro = GyroMode::RightStick; break;
    case IdLayoutPosition:
    case IdLayoutName:
      g_cfg.buttons = id == IdLayoutName ? NintendoLabelMap() : DefaultPositionalMap();
      break;
    case IdNoProfile: g_cfg.profile.clear(); break;
    case IdEditButtons:
      OpenEditor();
      return;
    case IdSteamUsing:
      g_cfg.steam = g_cfg.steam == SteamMode::Always ? g_steam_base : SteamMode::Always;
      break;
    case IdSteamAuto:
      g_steam_base = g_steam_base == SteamMode::Auto ? SteamMode::Never : SteamMode::Auto;
      if (g_cfg.steam != SteamMode::Always) g_cfg.steam = g_steam_base;
      break;
    case IdHomeLight: g_cfg.home_light = !g_cfg.home_light; break;
    case IdRecalibrate:
      g_engine.RecalibrateGyro();
      return;
    case IdAutostart:
      SetAutostart(!AutostartEnabled());
      return;
    case IdOpenConfig:
      CreateDirectoryW(ConfigDir().c_str(), nullptr);
      ShellExecuteW(nullptr, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      return;
    case IdExit:
      DestroyWindow(g_wnd);
      return;
    default:
      return;
  }
  ApplySettings();
}

void OnEngineEvent(const EventMsg& m) {
  switch (m.event) {
    case EngineEvent::Connected:
      g_low_battery_warned = false;
      Notify(Widen(m.text));
      break;
    case EngineEvent::Disconnected:
    case EngineEvent::PoweredOff:
    case EngineEvent::Paused:
    case EngineEvent::Resumed:
      Notify(Widen(m.text));
      break;
    case EngineEvent::DriverError:
      Notify(Widen(m.text), true);
      break;
    case EngineEvent::Info:
      break;  // só aparece no console
  }
  UpdateTip();
}

void CheckBattery() {
  EngineStatus st = g_engine.GetStatus();
  if (!st.connected || st.usb || st.charging) {
    if (st.charging) g_low_battery_warned = false;
    return;
  }
  if (st.battery <= 2 && !g_low_battery_warned) {
    g_low_battery_warned = true;
    Notify(L"Bateria do Pro Controller baixa. Conecte o cabo para carregar.", true);
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g_taskbar_created && g_taskbar_created != 0) {
    // O Explorer reiniciou: o ícone sumiu e precisa ser recriado.
    AddTrayIcon();
    UpdateTip();
    return 0;
  }
  switch (msg) {
    case kTrayMsg:
      if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP ||
          LOWORD(lp) == WM_CONTEXTMENU) {
        ShowMenu();
      }
      return 0;
    case kEngineMsg: {
      auto* m = reinterpret_cast<EventMsg*>(lp);
      OnEngineEvent(*m);
      delete m;
      return 0;
    }
    case WM_COMMAND:
      OnCommand(LOWORD(wp));
      return 0;
    case WM_TIMER:
      UpdateTip();
      CheckBattery();
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kTimerId);
      g_engine.Stop();
      RemoveTrayIcon();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
  // Só uma cópia aberta por vez (as duas brigariam pelo controle).
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ProconWinTray");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(nullptr, L"O ProconWin já está aberto. Procure o ícone ao lado do relógio.",
                L"ProconWin", MB_OK | MB_ICONINFORMATION);
    return 0;
  }

  g_inst = inst;
  g_cfg = LoadSettings();
  if (g_cfg.steam != SteamMode::Always) g_steam_base = g_cfg.steam;

  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = L"ProconWinTray";
  RegisterClassW(&wc);
  // Janela invisível só para receber as mensagens do ícone.
  g_wnd = CreateWindowW(wc.lpszClassName, L"ProconWin", 0, 0, 0, 0, 0, nullptr, nullptr, inst,
                        nullptr);
  if (!g_wnd) return 1;
  g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

  AddTrayIcon();
  SetTimer(g_wnd, kTimerId, 1000, nullptr);

  HWND wnd = g_wnd;
  g_engine.Start(g_cfg, [wnd](EngineEvent e, const std::string& text) {
    auto* m = new EventMsg{e, text};
    if (!PostMessageW(wnd, kEngineMsg, 0, reinterpret_cast<LPARAM>(m))) delete m;
  });
  UpdateTip();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    // Tab, Enter e Esc dentro do editor.
    HWND editor = ButtonEditorWindow();
    if (editor && IsDialogMessageW(editor, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (mutex) CloseHandle(mutex);
  return 0;
}
