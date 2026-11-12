#include "updater.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "procon/update.h"
#include "storage.h"
#include "version.h"


namespace updater {

namespace {

constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/Nerqs/proconwin/releases/latest";
constexpr size_t kMaxInstaller = 200u * 1024 * 1024;

std::mutex g_mu;
Status g_status;
procon::ReleaseInfo g_release;
std::wstring g_installer_path;
HWND g_notify = nullptr;
UINT g_msg = 0;
bool g_busy = false;

void SetState(State s, const std::string& error = {}) {
  {
    std::lock_guard<std::mutex> l(g_mu);
    g_status.state = s;
    g_status.error = error;
  }
  if (g_notify) PostMessageW(g_notify, g_msg, static_cast<WPARAM>(s), 0);
}

struct Handle {
  HINTERNET h = nullptr;
  explicit Handle(HINTERNET x) : h(x) {}
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};

// GET por HTTPS, seguindo redirecionamentos (o GitHub manda os arquivos para
// outro servidor). on_progress recebe (baixado, total ou 0).
bool HttpGet(const std::wstring& url, std::string& body, std::string& err,
             void (*on_progress)(size_t, size_t) = nullptr, size_t max = 4u * 1024 * 1024) {
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256], path[2048];
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  wchar_t extra[2048];
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
    err = "endereço inválido";
    return false;
  }
  std::wstring full_path = std::wstring(path) + extra;

  std::wstring agent = L"ProconWin/" + Widen(PROCONWIN_VERSION);
  Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session.h) {
    // Windows antigo sem o proxy automático.
    session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session.h) {
    err = "WinHttpOpen falhou";
    return false;
  }
  WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
  Handle conn(WinHttpConnect(session.h, host, uc.nPort, 0));
  if (!conn.h) {
    err = "sem conexão";
    return false;
  }
  Handle req(WinHttpOpenRequest(conn.h, L"GET", full_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!req.h) {
    err = "pedido falhou";
    return false;
  }
  const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
  if (!WinHttpSendRequest(req.h, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0,
                          0) ||
      !WinHttpReceiveResponse(req.h, nullptr)) {
    err = "sem internet ou o GitHub não respondeu (erro " + std::to_string(GetLastError()) + ")";
    return false;
  }
  DWORD code = 0, size = sizeof(code);
  WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
  if (code != 200) {
    err = code == 404 ? "nenhuma versão publicada ainda" : "o GitHub respondeu " + std::to_string(code);
    return false;
  }
  DWORD total = 0;
  size = sizeof(total);
  if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &total, &size, WINHTTP_NO_HEADER_INDEX)) {
    total = 0;
  }
  body.clear();
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req.h, &avail)) {
      err = "o download parou no meio";
      return false;
    }
    if (avail == 0) break;
    size_t old = body.size();
    if (old + avail > max) {
      err = "arquivo grande demais";
      return false;
    }
    body.resize(old + avail);
    DWORD got = 0;
    if (!WinHttpReadData(req.h, &body[old], avail, &got)) {
      err = "o download parou no meio";
      return false;
    }
    body.resize(old + got);
    if (on_progress) on_progress(body.size(), total);
  }
  return true;
}

std::string Sha256Hex(const std::string& data) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  unsigned char out[32];
  std::string hex;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return hex;
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                     static_cast<ULONG>(data.size()), 0) == 0 &&
      BCryptFinishHash(hash, out, sizeof(out), 0) == 0) {
    char buf[3];
    for (unsigned char b : out) {
      std::snprintf(buf, sizeof(buf), "%02x", b);
      hex += buf;
    }
  }
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  return hex;
}

void CheckThread() {
  std::string body, err;
  std::wstring url = std::wstring(L"https://") + kApiHost + kApiPath;
  procon::ReleaseInfo r;
  if (!HttpGet(url, body, err)) {
    SetState(State::Failed, err);
  } else if (!procon::ParseLatestRelease(body, r)) {
    SetState(State::Failed, "a versão publicada não tem instalador");
  } else if (procon::CompareVersions(r.version, PROCONWIN_VERSION) > 0) {
    {
      std::lock_guard<std::mutex> l(g_mu);
      g_release = r;
      g_status.latest = r.version;
      g_status.page_url = r.page_url;
    }
    SetState(State::Available);
  } else {
    SetState(State::UpToDate);
  }
  std::lock_guard<std::mutex> l(g_mu);
  g_busy = false;
}

void Progress(size_t got, size_t total) {
  std::lock_guard<std::mutex> l(g_mu);
  g_status.progress = total ? static_cast<float>(got) / static_cast<float>(total) : 0.0f;
}

void DownloadThread() {
  procon::ReleaseInfo r;
  {
    std::lock_guard<std::mutex> l(g_mu);
    r = g_release;
  }
  std::string body, err;
  if (!HttpGet(Widen(r.installer_url), body, err, Progress, kMaxInstaller)) {
    SetState(State::Failed, "não consegui baixar: " + err);
  } else if (!r.sha256.empty() && Sha256Hex(body) != r.sha256) {
    SetState(State::Failed, "o arquivo baixado não confere (SHA-256); tente de novo");
  } else {
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp, n) + L"ProconWin-Setup-" + Widen(r.version) + L".exe";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wrote = 0;
    bool ok = f != INVALID_HANDLE_VALUE &&
              WriteFile(f, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr) &&
              wrote == body.size();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) {
      SetState(State::Failed, "não consegui salvar o instalador na pasta temporária");
    } else {
      {
        std::lock_guard<std::mutex> l(g_mu);
        g_installer_path = path;
        g_status.progress = 1;
      }
      SetState(State::Ready);
    }
  }
  std::lock_guard<std::mutex> l(g_mu);
  g_busy = false;
}

}  // namespace

void Init(HWND notify, UINT msg) {
  g_notify = notify;
  g_msg = msg;
  std::lock_guard<std::mutex> l(g_mu);
  g_status.current = PROCONWIN_VERSION;
}

Status Get() {
  std::lock_guard<std::mutex> l(g_mu);
  return g_status;
}

void Check() {
  {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_busy || g_status.state == State::Ready) return;
    g_busy = true;
    g_status.state = State::Checking;
  }
  std::thread(CheckThread).detach();
}

void Download() {
  {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_busy || g_release.installer_url.empty()) return;
    g_busy = true;
    g_status.state = State::Downloading;
    g_status.progress = 0;
    g_status.error.clear();
  }
  std::thread(DownloadThread).detach();
}

bool RunInstaller() {
  std::wstring path;
  {
    std::lock_guard<std::mutex> l(g_mu);
    path = g_installer_path;
  }
  if (path.empty()) return false;
  // /SILENT mostra só o progresso; o instalador fecha o ProconWin, atualiza
  // o programa e o driver, e abre o ProconWin de novo no fim.
  HINSTANCE r = ShellExecuteW(nullptr, L"open", path.c_str(), L"/SILENT /SUPPRESSMSGBOXES /NORESTART",
                              nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(r) > 32;
}

}  // namespace updater
