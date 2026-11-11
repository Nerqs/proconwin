#include "desktop_input.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace procon {

namespace {

// Teclas que precisam da marca "estendida" para não virarem as do teclado
// numérico.
bool Extended(WORD vk) {
  switch (vk) {
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
    case VK_PRIOR: case VK_NEXT: case VK_DELETE: case VK_LWIN:
      return true;
    default:
      return false;
  }
}

void AddKey(std::vector<INPUT>& v, WORD vk, bool up) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
  in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (Extended(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
  v.push_back(in);
}

void AddMouse(std::vector<INPUT>& v, DWORD flags, int data = 0) {
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags;
  in.mi.mouseData = static_cast<DWORD>(data);
  v.push_back(in);
}

// Atalho de várias teclas: aperta todas e solta na ordem inversa.
void AddCombo(std::vector<INPUT>& v, std::initializer_list<WORD> keys) {
  for (WORD k : keys) AddKey(v, k, false);
  for (auto it = keys.end(); it != keys.begin();) AddKey(v, *--it, true);
}

WORD SimpleKey(PcAction a) {
  switch (a) {
    case PcAction::Enter: return VK_RETURN;
    case PcAction::Esc: return VK_ESCAPE;
    case PcAction::Space: return VK_SPACE;
    case PcAction::Backspace: return VK_BACK;
    case PcAction::Tab: return VK_TAB;
    case PcAction::Delete: return VK_DELETE;
    case PcAction::Up: return VK_UP;
    case PcAction::Down: return VK_DOWN;
    case PcAction::Left: return VK_LEFT;
    case PcAction::Right: return VK_RIGHT;
    case PcAction::PageUp: return VK_PRIOR;
    case PcAction::PageDown: return VK_NEXT;
    case PcAction::Windows: return VK_LWIN;
    case PcAction::VolumeUp: return VK_VOLUME_UP;
    case PcAction::VolumeDown: return VK_VOLUME_DOWN;
    case PcAction::Mute: return VK_VOLUME_MUTE;
    case PcAction::PlayPause: return VK_MEDIA_PLAY_PAUSE;
    case PcAction::Back: return VK_BROWSER_BACK;
    case PcAction::Forward: return VK_BROWSER_FORWARD;
    default: return 0;
  }
}

void AddEvent(std::vector<INPUT>& v, const PcEvent& e) {
  bool down = e.kind != PcEvent::Up;
  switch (e.action) {
    case PcAction::LeftClick:
      if (e.kind != PcEvent::Repeat)
        AddMouse(v, down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
      return;
    case PcAction::RightClick:
      if (e.kind != PcEvent::Repeat)
        AddMouse(v, down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
      return;
    case PcAction::MiddleClick:
      if (e.kind != PcEvent::Repeat)
        AddMouse(v, down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP);
      return;
    // Atalhos: disparam uma vez ao apertar.
    case PcAction::AltTab:
      if (e.kind == PcEvent::Down) AddCombo(v, {VK_MENU, VK_TAB});
      return;
    case PcAction::TaskView:
      if (e.kind == PcEvent::Down) AddCombo(v, {VK_LWIN, VK_TAB});
      return;
    case PcAction::CloseWindow:
      if (e.kind == PcEvent::Down) AddCombo(v, {VK_MENU, VK_F4});
      return;
    case PcAction::Keyboard:
      if (e.kind == PcEvent::Down) AddCombo(v, {VK_LWIN, VK_CONTROL, 'O'});
      return;
    case PcAction::Screenshot:
      if (e.kind == PcEvent::Down) AddCombo(v, {VK_LWIN, VK_SHIFT, 'S'});
      return;
    default:
      break;
  }
  // Tecla comum: fica apertada enquanto o botão estiver apertado.
  if (WORD vk = SimpleKey(e.action)) AddKey(v, vk, !down);
}

}  // namespace

void SendDesktopFrame(const DesktopFrame& f) {
  std::vector<INPUT> v;
  if (f.mouse_dx || f.mouse_dy) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = f.mouse_dx;
    in.mi.dy = f.mouse_dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    v.push_back(in);
  }
  if (f.wheel) AddMouse(v, MOUSEEVENTF_WHEEL, f.wheel);
  if (f.hwheel) AddMouse(v, MOUSEEVENTF_HWHEEL, f.hwheel);
  for (const auto& e : f.events) AddEvent(v, e);
  if (!v.empty()) SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
}

}  // namespace procon
