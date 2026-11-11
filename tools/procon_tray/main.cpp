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

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "engine.h"
#include "procon/settings.h"

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
  IdExit,
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

std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::wstring ConfigDir() {
  PWSTR path = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
    dir = std::wstring(path) + L"\\ProconWin";
  }
  CoTaskMemFree(path);
  return dir;
}

std::wstring ConfigPath() { return ConfigDir() + L"\\config.ini"; }

Settings LoadSettings() {
  std::ifstream f(std::filesystem::path(ConfigPath()), std::ios::binary);
  if (!f) return Settings{};
  std::stringstream ss;
  ss << f.rdbuf();
  return ParseSettings(ss.str());
}

void SaveSettings(const Settings& s) {
  CreateDirectoryW(ConfigDir().c_str(), nullptr);
  std::ofstream f(std::filesystem::path(ConfigPath()), std::ios::binary | std::ios::trunc);
  f << SerializeSettings(s);
}

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
  AppendMenuW(layout, MF_STRING, IdLayoutPosition, L"Pela posição (B da Nintendo = A do Xbox)");
  AppendMenuW(layout, MF_STRING, IdLayoutName, L"Pelo nome (A da Nintendo = A do Xbox)");
  CheckMenuRadioItem(layout, IdLayoutPosition, IdLayoutName,
                     g_cfg.nintendo_layout ? IdLayoutName : IdLayoutPosition, MF_BYCOMMAND);

  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, StatusText().c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gyro), L"Giroscópio");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layout), L"Layout dos botões");
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
  switch (id) {
    case IdGyroOff: g_cfg.gyro = GyroMode::Off; break;
    case IdGyroMouse: g_cfg.gyro = GyroMode::Mouse; break;
    case IdGyroStick: g_cfg.gyro = GyroMode::RightStick; break;
    case IdLayoutPosition: g_cfg.nintendo_layout = false; break;
    case IdLayoutName: g_cfg.nintendo_layout = true; break;
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
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (mutex) CloseHandle(mutex);
  return 0;
}
