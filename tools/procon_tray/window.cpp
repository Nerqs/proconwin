// Janela principal: Win32 + Direct3D 11, com a interface desenhada pelo
// Dear ImGui (ui/app_ui.cpp). Só desenha enquanto está visível.
#include "window.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "storage.h"
#include "updater.h"
#include "ui/app_ui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wp,
                                                             LPARAM lp);

using namespace procon;

namespace {

constexpr wchar_t kClass[] = L"ProconWinMain";
constexpr UINT_PTR kFrameTimer = 1;
constexpr int kBaseW = 1100, kBaseH = 780;  // tamanho inicial a 100%
constexpr int kMinW = 940, kMinH = 660;

HWND g_hwnd = nullptr;
AppHooks g_hooks;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_imgui_ready = false;
float g_scale = 1.0f;
ui::Fonts g_fonts;
bool g_fonts_dirty = false;

template <typename T>
void Release(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

float DpiScale(HWND hwnd) {
  // GetDpiForWindow existe a partir do Windows 10 1607.
  using Fn = UINT(WINAPI*)(HWND);
  static Fn fn = reinterpret_cast<Fn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
  UINT dpi = 96;
  if (fn && hwnd) {
    dpi = fn(hwnd);
  } else {
    HDC dc = GetDC(nullptr);
    dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
    ReleaseDC(nullptr, dc);
  }
  return dpi > 0 ? dpi / 96.0f : 1.0f;
}

// --- Direct3D -----------------------------------------------------------------

void CreateTarget() {
  ID3D11Texture2D* back = nullptr;
  if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
  }
}

bool CreateDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  // Placa de vídeo; se não der, o renderizador de software do Windows (WARP).
  for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
    for (DXGI_SWAP_EFFECT effect : {DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_DISCARD}) {
      sd.SwapEffect = effect;
      sd.BufferCount = effect == DXGI_SWAP_EFFECT_DISCARD ? 1 : 2;
      HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, 2,
                                                 D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got,
                                                 &g_ctx);
      if (SUCCEEDED(hr)) {
        CreateTarget();
        return true;
      }
    }
  }
  return false;
}

void DestroyDevice() {
  Release(g_rtv);
  Release(g_swap);
  Release(g_ctx);
  Release(g_device);
}

// --- fontes e tema ------------------------------------------------------------

std::string FontPath(const char* file) {
  wchar_t dir[MAX_PATH];
  UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  return Narrow(std::wstring(dir) + L"\\Fonts\\") + file;
}

ImFont* AddFont(const char* file, const char* fallback, float size) {
  ImGuiIO& io = ImGui::GetIO();
  const ImWchar* ranges = reinterpret_cast<const ImWchar*>(ui::GlyphRanges());
  for (const char* f : {file, fallback}) {
    std::string path = FontPath(f);
    if (!path.empty() && GetFileAttributesW(Widen(path).c_str()) != INVALID_FILE_ATTRIBUTES) {
      if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size, nullptr, ranges)) {
        return font;
      }
    }
  }
  ImFontConfig cfg;
  cfg.SizePixels = size;
  return io.Fonts->AddFontDefault(&cfg);
}

void BuildFonts() {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  float s = g_scale;
  g_fonts.regular = AddFont("segoeui.ttf", "arial.ttf", 16.0f * s);
  g_fonts.bold = AddFont("seguisb.ttf", "segoeuib.ttf", 16.0f * s);
  g_fonts.title = AddFont("segoeuib.ttf", "arialbd.ttf", 24.0f * s);
  g_fonts.pad = AddFont("seguisb.ttf", "segoeuib.ttf", 14.0f * s);
  io.FontDefault = g_fonts.regular;
  ui::ApplyTheme(s);
}

// --- o modelo que a interface usa --------------------------------------------

static_assert(static_cast<int>(updater::State::Failed) == ui::UpdateView::Failed,
              "os dois enums têm que ter a mesma ordem");

class Model : public ui::UiModel {
 public:
  Settings Get() override { return g_hooks.get(); }
  void Apply(const Settings& s) override { g_hooks.apply(s); }
  EngineStatus Status() override { return g_hooks.status(); }
  std::string Diagnostics() override { return DiagnosticsText(g_hooks.status(), g_hooks.get()); }
  bool Autostart() override { return g_hooks.autostart(); }
  void SetAutostart(bool on) override { g_hooks.set_autostart(on); }
  SteamMode SteamBase() override { return g_hooks.steam_base(); }
  void SetSteamBase(SteamMode m) override { g_hooks.set_steam_base(m); }
  void Recalibrate() override { g_hooks.recalibrate(); }
  void OpenFolder() override { g_hooks.open_folder(); }
  void HideWindow() override { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }
  void Exit() override { g_hooks.exit_app(); }
  ui::UpdateView Update() override {
    updater::Status u = updater::Get();
    ui::UpdateView v;
    v.state = static_cast<ui::UpdateView::State>(u.state);
    v.current = u.current;
    v.latest = u.latest;
    v.error = u.error;
    v.progress = u.progress;
    return v;
  }
  void CheckUpdate() override { updater::Check(); }
  void StartUpdate() override { updater::Download(); }
  void OpenReleasePage() override {
    std::string url = updater::Get().page_url;
    if (url.empty()) url = "https://github.com/Nerqs/proconwin/releases";
    ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
  std::vector<std::string> ListProfiles() override { return ::ListProfiles(); }
  bool LoadProfile(const std::string& n, Settings& out) override { return ::LoadProfile(n, out); }
  bool SaveProfile(const std::string& n, const Settings& s) override {
    return ::SaveProfile(n, s);
  }
  bool DeleteProfile(const std::string& n) override { return ::DeleteProfile(n); }
};
Model g_model;

// --- quadro -------------------------------------------------------------------

bool g_in_frame = false;

void RenderFrame() {
  if (g_in_frame) return;  // algo chamado de dentro da interface pediu outro quadro
  struct Guard {
    Guard() { g_in_frame = true; }
    ~Guard() { g_in_frame = false; }
  } guard;
  if (!g_imgui_ready || !g_swap || !IsWindowVisible(g_hwnd) || IsIconic(g_hwnd)) return;
  if (g_fonts_dirty) {
    g_fonts_dirty = false;
    BuildFonts();
    ImGui_ImplDX11_InvalidateDeviceObjects();  // a textura das letras é refeita no NewFrame
  }
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  ui::DrawApp(g_model, g_fonts, g_scale);
  ImGui::Render();
  const float bg[4] = {18 / 255.f, 20 / 255.f, 25 / 255.f, 1};
  g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
  g_ctx->ClearRenderTargetView(g_rtv, bg);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  HRESULT hr = g_swap->Present(1, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    // Placa de vídeo reiniciou (driver atualizado, por exemplo): recria tudo.
    ImGui_ImplDX11_Shutdown();
    DestroyDevice();
    if (CreateDevice(g_hwnd)) {
      ImGui_ImplDX11_Init(g_device, g_ctx);
    } else {
      g_imgui_ready = false;
    }
  }
}

void UseDarkTitleBar(HWND hwnd) {
  // DWMWA_USE_IMMERSIVE_DARK_MODE (20) no Windows 10 20H1+ e 11.
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
  // Cantos arredondados do Windows 11 (DWMWA_WINDOW_CORNER_PREFERENCE = 33, ROUND = 2).
  int corner = 2;
  DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (g_imgui_ready && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
  switch (msg) {
    case WM_TIMER:
      if (wp == kFrameTimer) RenderFrame();
      return 0;
    case WM_SIZE:
      if (g_device && wp != SIZE_MINIMIZED) {
        Release(g_rtv);
        g_swap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
        CreateTarget();
        RenderFrame();
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      float s = hwnd ? DpiScale(hwnd) : 1.0f;
      mm->ptMinTrackSize.x = static_cast<LONG>(kMinW * s);
      mm->ptMinTrackSize.y = static_cast<LONG>(kMinH * s);
      return 0;
    }
    case 0x02E0: {  // WM_DPICHANGED: mudou de monitor ou de escala
      g_scale = HIWORD(wp) / 96.0f;
      g_fonts_dirty = true;
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_CLOSE:
      // Fechar só esconde: o programa continua na bandeja.
      KillTimer(hwnd, kFrameTimer);
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kFrameTimer);
      if (g_imgui_ready) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imgui_ready = false;
      }
      DestroyDevice();
      g_hwnd = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool CreateMainWindow(HINSTANCE inst) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);

  // Tamanho inicial pela escala do monitor principal; centralizado.
  g_scale = DpiScale(nullptr);
  int w = static_cast<int>(kBaseW * g_scale), h = static_cast<int>(kBaseH * g_scale);
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  w = std::min<int>(w, work.right - work.left);
  h = std::min<int>(h, work.bottom - work.top);
  int x = work.left + (work.right - work.left - w) / 2;
  int y = work.top + (work.bottom - work.top - h) / 2;
  g_hwnd = CreateWindowExW(0, kClass, L"ProconWin", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr,
                           nullptr, inst, nullptr);
  if (!g_hwnd) return false;
  UseDarkTitleBar(g_hwnd);
  g_scale = DpiScale(g_hwnd);

  if (!CreateDevice(g_hwnd)) {
    MessageBoxW(nullptr, L"Não consegui iniciar o Direct3D 11 para desenhar a janela.",
                L"ProconWin", MB_ICONERROR);
    DestroyWindow(g_hwnd);
    return false;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // nada de imgui.ini na pasta
  io.LogFilename = nullptr;
  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX11_Init(g_device, g_ctx);
  BuildFonts();
  g_imgui_ready = true;
  return true;
}

}  // namespace

void ShowMainWindow(HINSTANCE inst, const AppHooks& hooks) {
  g_hooks = hooks;
  if (!g_hwnd && !CreateMainWindow(inst)) return;
  if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
  ShowWindow(g_hwnd, SW_SHOW);
  SetForegroundWindow(g_hwnd);
  SetTimer(g_hwnd, kFrameTimer, 16, nullptr);  // ~60 quadros por segundo
  RenderFrame();
}

void RefreshMainWindow() {
  // A interface lê tudo de novo a cada quadro (o próximo já mostra a mudança).
}

bool HandleWindowMessage(MSG*) {
  return false;  // o Dear ImGui trata teclado (Tab, Enter, Esc) dentro da janela
}
