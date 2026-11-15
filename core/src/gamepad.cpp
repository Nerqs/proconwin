#include "procon/gamepad.h"

#include <hidapi.h>

#include <chrono>
#include <cstring>

#include "procon/controller.h"
#include "procon/eightbitdo.h"

namespace procon {

namespace {

constexpr float kDeadzone = 0.08f;
using Clock = std::chrono::steady_clock;

// Pro Controller: só repassa para a classe Controller.
class ProControllerPad : public Gamepad {
 public:
  explicit ProControllerPad(std::unique_ptr<Controller> c) : c_(std::move(c)) {}

  std::string Name() const override { return "Pro Controller"; }
  bool IsUsb() const override { return c_->IsUsb(); }
  bool Initialize(std::string& error) override { return c_->Initialize(error); }

  int Poll(InputState& out, int timeout_ms) override {
    uint8_t buf[64];
    int n = c_->ReadReport(buf, sizeof(buf), timeout_ms);
    if (n < 0) return -1;
    if (n == 0 || buf[0] != kInputFull) return 0;
    return ParseFullReport(buf, static_cast<size_t>(n), c_->Cal(), kDeadzone, out) ? 1 : 0;
  }

  bool Rumble(uint8_t big, uint8_t little) override {
    if (!big && !little) return c_->SendRumble(kRumbleNeutral, kRumbleNeutral);
    // Motor forte = frequência baixa, fraco = alta.
    auto side = EncodeRumble(320.0f, little / 255.0f, 160.0f, big / 255.0f);
    return c_->SendRumble(side, side);
  }

  void SetPlayer(int player) override {
    c_->SetPlayerLights(static_cast<uint8_t>(1u << (player % 4)));
  }
  void SetHomeLight(bool on) override { c_->SetHomeLight(on); }
  bool CanPowerOff() const override { return !c_->IsUsb(); }
  bool PowerOff() override { return c_->PowerOff(); }
  bool NeedsWatchdog() const override { return true; }

 private:
  std::unique_ptr<Controller> c_;
};

// 8BitDo Ultimate 2 Wireless pelo Bluetooth.
class EightBitDoPad : public Gamepad {
 public:
  EightBitDoPad(hid_device* dev, std::string path) : dev_(dev), path_(std::move(path)) {}
  ~EightBitDoPad() override {
    if (rumble_) Rumble(0, 0);
    hid_close(dev_);
  }

  std::string Name() const override { return "8BitDo Ultimate 2"; }
  bool IsUsb() const override { return false; }

  bool Initialize(std::string& error) override {
    // Espera o primeiro relatório: pelo tamanho dá para saber se o firmware
    // manda giroscópio, bateria e aceita vibração (1.03 em diante). Logo
    // depois de conectar ele pode levar uns 250 ms.
    auto deadline = Clock::now() + std::chrono::milliseconds(1000);
    uint8_t buf[64];
    while (Clock::now() < deadline) {
      int n = hid_read_timeout(dev_, buf, sizeof(buf), 100);
      if (n < 0) break;
      if (n == 0) continue;
      full_ = static_cast<size_t>(n) >= k8BitDoFullReportSize;
      last_rx_ = Clock::now();
      return true;
    }
    error = "o 8BitDo não mandou nada; aperte um botão nele";
    return false;
  }

  int Poll(InputState& out, int timeout_ms) override {
    uint8_t buf[64];
    int n = hid_read_timeout(dev_, buf, sizeof(buf), timeout_ms);
    if (n < 0) return -1;
    auto now = Clock::now();
    if (n == 0) {
      // Mudo há um tempo: confere se o Windows ainda lista o controle (se ele
      // desligou, a leitura pode só ficar sem resposta em vez de dar erro).
      if (now - last_rx_ > std::chrono::seconds(3) &&
          now - last_check_ > std::chrono::seconds(3)) {
        last_check_ = now;
        if (!StillPresent()) return -1;
      }
      return 0;
    }
    last_rx_ = now;
    if (static_cast<size_t>(n) >= k8BitDoFullReportSize) full_ = true;
    return Parse8BitDoReport(buf, static_cast<size_t>(n), kDeadzone, out) ? 1 : 0;
  }

  bool Rumble(uint8_t big, uint8_t little) override {
    if (!full_) return false;  // firmware antigo não vibra
    // O motor fica no último valor, e no Bluetooth LE cada escrita é lenta:
    // só manda quando muda (ou de vez em quando, por garantia).
    auto now = Clock::now();
    if (big == last_big_ && little == last_little_ &&
        now - last_rumble_ < std::chrono::milliseconds(500)) {
      return true;
    }
    uint8_t msg[5] = {k8BitDoOutputRumble, big, little, 0, 0};
    rumble_ = big || little;
    last_big_ = big;
    last_little_ = little;
    last_rumble_ = now;
    return hid_write(dev_, msg, sizeof(msg)) >= 0;
  }

 private:
  bool StillPresent() const {
    hid_device_info* list = hid_enumerate(kVendor8BitDo, kProduct8BitDoUltimate2);
    bool found = false;
    for (const hid_device_info* d = list; d; d = d->next) {
      if (path_ == d->path) found = true;
    }
    hid_free_enumeration(list);
    return found;
  }

  hid_device* dev_;
  std::string path_;
  Clock::time_point last_rx_ = Clock::now();
  Clock::time_point last_check_ = Clock::now();
  bool full_ = false;
  bool rumble_ = false;
  uint8_t last_big_ = 0, last_little_ = 0;
  Clock::time_point last_rumble_{};
};

bool IsBluetooth(const hid_device_info* d) {
#if defined(HID_API_VERSION) && HID_API_VERSION >= HID_API_MAKE_VERSION(0, 13, 0)
  return d->bus_type == HID_API_BUS_BLUETOOTH;
#else
  return d->interface_number < 0;
#endif
}

// Só pelo Bluetooth: pelo receptor 2.4G o 8BitDo já é um controle de Xbox, e
// abrir ele aqui faria aparecer um controle a mais.
std::unique_ptr<Gamepad> Open8BitDo(std::string& error) {
  hid_device_info* list = hid_enumerate(kVendor8BitDo, kProduct8BitDoUltimate2);
  const hid_device_info* pick = nullptr;
  for (const hid_device_info* d = list; d; d = d->next) {
    if (!IsBluetooth(d)) continue;
    // Prefere a coleção de controle (página 1, uso 5 = gamepad, 4 = joystick).
    bool gamepad = d->usage_page == 0x01 && (d->usage == 0x05 || d->usage == 0x04);
    if (!pick || gamepad) pick = d;
    if (gamepad) break;
  }
  if (!pick) {
    hid_free_enumeration(list);
    return nullptr;
  }
  std::string path = pick->path;
  hid_device* dev = hid_open_path(pick->path);
  hid_free_enumeration(list);
  if (!dev) {
    error = "achei o 8BitDo, mas não consegui abrir. Feche programas que estejam usando o controle";
    return nullptr;
  }
  return std::make_unique<EightBitDoPad>(dev, path);
}

}  // namespace

std::unique_ptr<Gamepad> OpenGamepad(std::string& error) {
  auto c = std::make_unique<Controller>();
  std::string pro_err;
  if (c->Open(pro_err)) return std::make_unique<ProControllerPad>(std::move(c));

  std::string bitdo_err;
  if (auto p = Open8BitDo(bitdo_err)) return p;

  // Mensagem do Pro Controller quando ele foi achado mas não abriu.
  bool pro_found = pro_err.rfind("Nenhum", 0) != 0;
  if (pro_found) error = pro_err;
  else if (!bitdo_err.empty()) error = bitdo_err;
  else error = "nenhum controle encontrado. Conecte o Pro Controller (cabo ou Bluetooth) "
               "ou o 8BitDo Ultimate 2 pelo Bluetooth";
  return nullptr;
}

}  // namespace procon
