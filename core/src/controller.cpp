#include "procon/controller.h"

#include <hidapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace procon {

namespace {

// Tamanho dos relatórios de saída. No Windows, mandar mais bytes do que o
// dispositivo declara faz a escrita falhar.
constexpr size_t kOutLenUsb = 64;
constexpr size_t kOutLenBt = 49;

std::string WideToUtf8(const wchar_t* w) {
  std::string s;
  if (!w) return s;
  for (; *w; ++w) s += (*w < 128) ? static_cast<char>(*w) : '?';
  return s;
}

using Clock = std::chrono::steady_clock;

}  // namespace

Controller::~Controller() { Close(); }

bool Controller::Open(std::string& error) {
  hid_device_info* list = hid_enumerate(kVendorNintendo, kProductProController);
  if (!list) {
    error = "Nenhum Pro Controller encontrado. Conecte pelo cabo USB ou emparelhe pelo Bluetooth.";
    return false;
  }
  path_ = list->path;
#if defined(HID_API_VERSION) && HID_API_VERSION >= HID_API_MAKE_VERSION(0, 13, 0)
  usb_ = list->bus_type == HID_API_BUS_USB;
#else
  usb_ = list->interface_number >= 0;
#endif
  std::string product = WideToUtf8(list->product_string);
  hid_free_enumeration(list);

  dev_ = hid_open_path(path_.c_str());
  if (!dev_) {
    error = "Achei o controle (" + product + "), mas não consegui abrir. "
            "Feche a Steam ou outros programas que estejam usando o controle e tente de novo.";
    return false;
  }
  return true;
}

void Controller::Close() {
  if (!dev_) return;
  // Para a vibração e devolve o controle a um estado limpo.
  SendRumble(kRumbleNeutral, kRumbleNeutral);
  if (usb_) UsbCommand(usbcmd::DisableForceUsb, false);
  hid_close(dev_);
  dev_ = nullptr;
}

bool Controller::Write(const uint8_t* data, size_t len) {
  size_t out_len = usb_ ? kOutLenUsb : kOutLenBt;
  std::vector<uint8_t> buf(out_len, 0);
  std::memcpy(buf.data(), data, std::min(len, out_len));
  return hid_write(dev_, buf.data(), buf.size()) >= 0;
}

int Controller::ReadReport(uint8_t* buf, size_t len, int timeout_ms) {
  return hid_read_timeout(dev_, buf, len, timeout_ms);
}

bool Controller::UsbCommand(uint8_t cmd, bool wait_reply) {
  uint8_t msg[2] = {kOutputUsb, cmd};
  if (!Write(msg, sizeof(msg))) return false;
  if (!wait_reply) return true;

  auto deadline = Clock::now() + std::chrono::milliseconds(500);
  uint8_t buf[64];
  while (Clock::now() < deadline) {
    int n = ReadReport(buf, sizeof(buf), 50);
    if (n >= 2 && buf[0] == kInputUsbReply && buf[1] == cmd) return true;
  }
  return false;
}

bool Controller::Subcommand(uint8_t id, const std::vector<uint8_t>& args,
                            std::vector<uint8_t>* reply) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::vector<uint8_t> msg;
    msg.push_back(kOutputRumbleSubcmd);
    msg.push_back(counter_);
    counter_ = (counter_ + 1) & 0x0F;
    msg.insert(msg.end(), rumble_l_.begin(), rumble_l_.end());
    msg.insert(msg.end(), rumble_r_.begin(), rumble_r_.end());
    msg.push_back(id);
    msg.insert(msg.end(), args.begin(), args.end());
    if (!Write(msg.data(), msg.size())) return false;

    auto deadline = Clock::now() + std::chrono::milliseconds(300);
    uint8_t buf[64];
    while (Clock::now() < deadline) {
      int n = ReadReport(buf, sizeof(buf), 30);
      // Resposta: byte 13 = ACK (bit 7 ligado), byte 14 = ID do subcomando.
      if (n >= 15 && buf[0] == kInputSubcmdReply && buf[14] == id) {
        if (reply) reply->assign(buf, buf + n);
        return (buf[13] & 0x80) != 0;
      }
    }
  }
  return false;
}

bool Controller::SpiRead(uint32_t addr, uint8_t size, std::vector<uint8_t>& out) {
  std::vector<uint8_t> args = {
      static_cast<uint8_t>(addr & 0xFF), static_cast<uint8_t>((addr >> 8) & 0xFF),
      static_cast<uint8_t>((addr >> 16) & 0xFF), static_cast<uint8_t>((addr >> 24) & 0xFF),
      size};
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::vector<uint8_t> reply;
    if (!Subcommand(subcmd::SpiRead, args, &reply)) continue;
    // Dados: bytes 15-18 = endereço, 19 = tamanho, 20+ = conteúdo.
    if (reply.size() < 20u + size) continue;
    uint32_t got = reply[15] | (reply[16] << 8) | (reply[17] << 16) |
                   (static_cast<uint32_t>(reply[18]) << 24);
    if (got != addr) continue;
    out.assign(reply.begin() + 20, reply.begin() + 20 + size);
    return true;
  }
  return false;
}

bool Controller::LoadCalibration() {
  std::vector<uint8_t> d;
  bool ok = true;

  // Analógicos: usa a calibração do usuário se existir, senão a de fábrica.
  std::vector<uint8_t> factory;
  if (SpiRead(spi::FactoryStickCal, 18, factory)) {
    cal_.left = DecodeLeftStickCal(factory.data());
    cal_.right = DecodeRightStickCal(factory.data() + 9);
  } else {
    ok = false;
  }
  if (SpiRead(spi::UserLeftStickCal, 11, d) && d[0] == 0xB2 && d[1] == 0xA1) {
    cal_.left = DecodeLeftStickCal(d.data() + 2);
    cal_.left_from_user = true;
  }
  if (SpiRead(spi::UserRightStickCal, 11, d) && d[0] == 0xB2 && d[1] == 0xA1) {
    cal_.right = DecodeRightStickCal(d.data() + 2);
    cal_.right_from_user = true;
  }

  // IMU
  if (SpiRead(spi::UserImuCal, 26, d) && d[0] == 0xB2 && d[1] == 0xA1) {
    cal_.imu = DecodeImuCal(d.data() + 2);
    cal_.imu_from_user = true;
  } else if (SpiRead(spi::FactoryImuCal, 24, d)) {
    cal_.imu = DecodeImuCal(d.data());
  } else {
    ok = false;
  }
  return ok;
}

bool Controller::LoadDeviceInfo() {
  std::vector<uint8_t> r;
  if (!Subcommand(subcmd::DeviceInfo, {}, &r) || r.size() < 25) return false;
  info_.fw_major = r[15];
  info_.fw_minor = r[16];
  info_.type = r[17];
  char mac[18];
  std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                r[19], r[20], r[21], r[22], r[23], r[24]);
  info_.mac = mac;

  std::vector<uint8_t> c;
  if (SpiRead(spi::Colors, 6, c)) {
    std::copy(c.begin(), c.begin() + 3, info_.body_color.begin());
    std::copy(c.begin() + 3, c.begin() + 6, info_.button_color.begin());
  }
  return true;
}

bool Controller::Initialize(std::string& error) {
  if (usb_) {
    // Handshake USB. Sem o ForceUsb, o controle tenta voltar ao Bluetooth.
    UsbCommand(usbcmd::Status, true);
    if (!UsbCommand(usbcmd::Handshake, true)) {
      error = "O controle não respondeu ao handshake USB.";
      return false;
    }
    UsbCommand(usbcmd::HighSpeed, true);
    UsbCommand(usbcmd::Handshake, true);
    UsbCommand(usbcmd::ForceUsb, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (!LoadDeviceInfo()) {
    error = "O controle não respondeu ao pedido de informações (subcomando 0x02).";
    return false;
  }
  if (!LoadCalibration()) {
    std::fprintf(stderr, "Aviso: não consegui ler toda a calibração; usando valores padrão.\n");
  }
  if (!Subcommand(subcmd::EnableImu, {0x01})) {
    std::fprintf(stderr, "Aviso: falha ao ligar o IMU.\n");
  }
  if (!Subcommand(subcmd::EnableVibration, {0x01})) {
    std::fprintf(stderr, "Aviso: falha ao ligar a vibração.\n");
  }
  if (!Subcommand(subcmd::SetReportMode, {kInputFull})) {
    error = "O controle não aceitou o modo completo (0x30).";
    return false;
  }
  SetPlayerLights(0x01);
  SetHomeLight(true);
  return true;
}

bool Controller::SendRumble(const std::array<uint8_t, 4>& left,
                            const std::array<uint8_t, 4>& right) {
  rumble_l_ = left;
  rumble_r_ = right;
  uint8_t msg[10];
  msg[0] = kOutputRumbleOnly;
  msg[1] = counter_;
  counter_ = (counter_ + 1) & 0x0F;
  std::memcpy(msg + 2, left.data(), 4);
  std::memcpy(msg + 6, right.data(), 4);
  return Write(msg, sizeof(msg));
}

bool Controller::SetPlayerLights(uint8_t mask) {
  return Subcommand(subcmd::PlayerLights, {mask});
}

bool Controller::SetHomeLight(bool on) {
  // Formato: duração/repetições + ciclos. Aqui: brilho fixo no máximo ou apagado.
  if (on) return Subcommand(subcmd::HomeLight, {0x01, 0xF0, 0xF0, 0x00});
  return Subcommand(subcmd::HomeLight, {0x00});
}

}  // namespace procon
