// Cliente do driver ProconWinXusb (modo usuário): manda o estado do controle
// para o controle de Xbox 360 virtual e recebe vibração/LED do jogo.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

#include "proconwin_xusb.h"

class XusbFeeder {
 public:
  XusbFeeder() = default;
  ~XusbFeeder();
  XusbFeeder(const XusbFeeder&) = delete;
  XusbFeeder& operator=(const XusbFeeder&) = delete;

  // Acha o driver, vira o "dono" do controle virtual e liga ele no XInput.
  bool Open(std::string& error);
  void Close();
  bool IsOpen() const { return device_ != INVALID_HANDLE_VALUE; }

  bool Submit(const ProconwinReport& report);

  // Último pedido do jogo (atualizado por uma thread interna).
  uint8_t LargeMotor() const { return large_.load(); }
  uint8_t SmallMotor() const { return small_.load(); }
  int Player() const { return player_.load(); }  // 0..3, ou -1

 private:
  void FeedbackLoop();

  HANDLE device_ = INVALID_HANDLE_VALUE;
  HANDLE stop_event_ = nullptr;
  std::thread thread_;
  std::atomic<uint8_t> large_{0}, small_{0};
  std::atomic<int> player_{-1};
};
