#include "xusb_feeder.h"

#include <setupapi.h>

#include <vector>

namespace {

const GUID kFeedInterface = PROCONWIN_FEED_INTERFACE_GUID_INIT;

// Caminho do primeiro dispositivo com a interface do nosso driver.
std::wstring FindDevicePath() {
  HDEVINFO set = SetupDiGetClassDevsW(&kFeedInterface, nullptr, nullptr,
                                      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (set == INVALID_HANDLE_VALUE) return {};
  std::wstring path;
  SP_DEVICE_INTERFACE_DATA iface{};
  iface.cbSize = sizeof(iface);
  if (SetupDiEnumDeviceInterfaces(set, nullptr, &kFeedInterface, 0, &iface)) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
    std::vector<BYTE> buf(needed);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
    detail->cbSize = sizeof(*detail);
    if (needed && SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, nullptr)) {
      path = detail->DevicePath;
    }
  }
  SetupDiDestroyDeviceInfoList(set);
  return path;
}

// DeviceIoControl síncrono num handle aberto com FILE_FLAG_OVERLAPPED.
bool Ioctl(HANDLE h, DWORD code, const void* in, DWORD in_len, void* out, DWORD out_len,
           DWORD* returned = nullptr) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return false;
  DWORD got = 0;
  BOOL ok = DeviceIoControl(h, code, const_cast<void*>(in), in_len, out, out_len, nullptr, &ov);
  if (!ok && GetLastError() == ERROR_IO_PENDING) ok = TRUE;
  if (ok) ok = GetOverlappedResult(h, &ov, &got, TRUE);
  DWORD err = GetLastError();
  CloseHandle(ov.hEvent);
  if (returned) *returned = got;
  SetLastError(err);
  return ok != FALSE;
}

}  // namespace

XusbFeeder::~XusbFeeder() { Close(); }

bool XusbFeeder::Open(std::string& error) {
  std::wstring path = FindDevicePath();
  if (path.empty()) {
    error = "Não encontrei o driver ProconWinXusb. Instale com driver\\instalar.ps1 "
            "(veja driver/README.md).";
    return false;
  }
  device_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED, nullptr);
  if (device_ == INVALID_HANDLE_VALUE) {
    error = "Não consegui abrir o driver (erro " + std::to_string(GetLastError()) + ").";
    return false;
  }
  if (!Ioctl(device_, IOCTL_PROCONWIN_REGISTER, nullptr, 0, nullptr, 0)) {
    DWORD e = GetLastError();
    error = e == ERROR_BUSY ? "Outro procon_virtual já está usando o controle virtual."
                            : "O driver recusou a conexão (erro " + std::to_string(e) + ").";
    Close();
    return false;
  }
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  thread_ = std::thread([this] { FeedbackLoop(); });
  return true;
}

void XusbFeeder::Close() {
  if (thread_.joinable()) {
    SetEvent(stop_event_);
    thread_.join();
  }
  if (stop_event_) {
    CloseHandle(stop_event_);
    stop_event_ = nullptr;
  }
  if (device_ != INVALID_HANDLE_VALUE) {
    // Fechar o handle desliga o controle virtual no XInput.
    CloseHandle(device_);
    device_ = INVALID_HANDLE_VALUE;
  }
}

bool XusbFeeder::Submit(const ProconwinReport& report) {
  return Ioctl(device_, IOCTL_PROCONWIN_SUBMIT, &report, sizeof(report), nullptr, 0);
}

void XusbFeeder::FeedbackLoop() {
  uint32_t seen = 0;
  for (;;) {
    ProconwinFeedback fb{};
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return;
    BOOL ok = DeviceIoControl(device_, IOCTL_PROCONWIN_WAIT_FEEDBACK, &seen, sizeof(seen), &fb,
                              sizeof(fb), nullptr, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      return;
    }
    HANDLE waits[] = {ov.hEvent, stop_event_};
    DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (w != WAIT_OBJECT_0) {
      CancelIoEx(device_, &ov);
    }
    DWORD got = 0;
    ok = GetOverlappedResult(device_, &ov, &got, TRUE);
    CloseHandle(ov.hEvent);
    if (w != WAIT_OBJECT_0 || !ok) return;
    if (got >= sizeof(fb)) {
      seen = fb.seq;
      large_ = fb.large_motor;
      small_ = fb.small_motor;
      player_ = fb.player == 0xFF ? -1 : fb.player;
    }
  }
}
