// Testes do núcleo (sem hardware). Rodar com: ctest --test-dir build
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "procon/gyro.h"
#include "procon/mapping.h"
#include "procon/protocol.h"
#include "procon/settings.h"
#include "procon/steam.h"

using namespace procon;

static int g_failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)

static bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

static void TestRumble() {
  CHECK(EncodeRumble(320, 0, 160, 0) == kRumbleNeutral);
  auto max = EncodeRumble(320, 1, 160, 1);
  CHECK(max[1] == 0xC9);  // amplitude alta máxima 0xC8 + bit alto da frequência
  CHECK(max[3] == 0x72);  // amplitude baixa máxima
}

static void TestSticks() {
  uint8_t d[3] = {0x00, 0x08, 0x80};
  int a, b;
  Decode12(d, a, b);
  CHECK(a == 2048 && b == 2048);
  CHECK(Near(NormalizeAxis(2048, 2048, 1600, 1600, 0.08f), 0));
  CHECK(Near(NormalizeAxis(3648, 2048, 1600, 1600, 0.08f), 1));
  CHECK(Near(NormalizeAxis(448, 2048, 1600, 1600, 0.08f), -1));
  // Dentro da zona morta (5%) fica em zero.
  CHECK(Near(NormalizeAxis(2128, 2048, 1600, 1600, 0.08f), 0));
}

static void TestParse() {
  uint8_t r[49] = {0x30, 1, 0x91, 0x08, 0, 0};
  Calibration c;
  InputState s;
  CHECK(ParseFullReport(r, sizeof(r), c, 0.08f, s));
  CHECK(s.battery_level == 8 && s.charging);
  CHECK(s.buttons == BtnA);
}

static void TestMapping() {
  InputState in;
  in.buttons = BtnB | BtnZR | BtnHome | BtnUp;
  in.rx = 1.0f;
  in.ly = -0.5f;
  XboxState x = MapToXbox(in, DefaultPositionalMap());
  CHECK(x.buttons == (xbtn::A | xbtn::Guide | xbtn::DpadUp));  // B da Nintendo = A do Xbox
  CHECK(x.right_trigger == 255 && x.left_trigger == 0);
  CHECK(x.rx == 32767);
  CHECK(x.ly == -16384 || x.ly == -16383);

  XboxState n = MapToXbox(in, NintendoLabelMap());
  CHECK(n.buttons & xbtn::B);
  CHECK(!(n.buttons & xbtn::A));

  // Botão remapeado para "nada" some.
  ButtonMap m = DefaultPositionalMap();
  m[static_cast<size_t>(Source::B)] = Target::None;
  CHECK(!(MapToXbox(in, m).buttons & xbtn::A));
}

static ImuSample Still(float gx, float gy, float gz) {
  ImuSample s{};
  s.accel_g[2] = 1.0f;
  s.gyro_dps[0] = gx;
  s.gyro_dps[1] = gy;
  s.gyro_dps[2] = gz;
  return s;
}

static void TestGyroCalibration() {
  GyroCalibrator cal;
  const float dt = 1.0f / 200.0f;
  // 1,5 s parado com drift constante: deve aprender o drift.
  for (int i = 0; i < 300; ++i) cal.AddSample(Still(1.6f, -0.5f, 0.3f), dt);
  CHECK(cal.HasBias());
  CHECK(Near(cal.Bias()[0], 1.6f) && Near(cal.Bias()[1], -0.5f) && Near(cal.Bias()[2], 0.3f));
  auto c = cal.Correct(Still(1.6f, -0.5f, 0.3f));
  CHECK(Near(c[0], 0) && Near(c[1], 0) && Near(c[2], 0));

  // Giro lento e constante de 10 °/s NÃO pode virar drift na calibração automática.
  for (int i = 0; i < 300; ++i) cal.AddSample(Still(1.6f, -0.5f, 10.0f), dt);
  CHECK(Near(cal.Bias()[2], 0.3f));

  // Movimento (giroscópio variando) não calibra.
  GyroCalibrator moving;
  for (int i = 0; i < 300; ++i) moving.AddSample(Still(0, 0, (i % 20) * 3.0f), dt);
  CHECK(!moving.HasBias());
}

static void TestGyroMouse() {
  GyroSettings s;
  s.mode = GyroMode::Mouse;
  s.mouse_pixels_per_degree = 10.0f;
  GyroOutput out(s);
  const float dt = 1.0f / 200.0f;
  // Girar para a direita (Z negativo) a 90 °/s por 1 s = 900 pixels para a direita.
  for (int i = 0; i < 200; ++i) out.AddSample({0, 0, -90.0f}, dt);
  int dx, dy;
  out.TakeMouseDelta(dx, dy);
  CHECK(dx >= 890 && dx <= 900);
  CHECK(dy == 0);
  // A parte fracionária não se perde.
  out.TakeMouseDelta(dx, dy);
  CHECK(dx == 0);
}

static void TestGyroStick() {
  GyroSettings s;
  s.mode = GyroMode::RightStick;
  s.stick_full_speed_dps = 360.0f;
  s.stick_anti_deadzone = 0.15f;
  GyroOutput out(s);
  const float dt = 1.0f / 200.0f;
  for (int i = 0; i < 50; ++i) out.AddSample({0, 0, -360.0f}, dt);
  CHECK(Near(out.StickX(), 1.0f));
  // Parado: analógico volta ao centro (sem o "pulo" do anti-deadzone).
  for (int i = 0; i < 50; ++i) out.AddSample({0, 0, 0.2f}, dt);
  CHECK(Near(out.StickX(), 0) && Near(out.StickY(), 0));
  // Movimento lento passa da zona morta do jogo.
  for (int i = 0; i < 50; ++i) out.AddSample({0, 0, -10.0f}, dt);
  CHECK(out.StickX() > 0.15f);
}

static void TestSteam() {
  std::vector<ProcessEntry> ps = {
      {4, 0, "System"},
      {100, 4, "explorer.exe"},
      {200, 100, "steam.exe"},
      {210, 200, "steamwebhelper.exe"},
      {211, 210, "steamwebhelper.exe"},
      {220, 200, "GameOverlayUI.exe"},
      {300, 100, "chrome.exe"},
  };
  // Só a Steam aberta, sem jogo.
  CHECK(!FindSteamGame(ps));

  // Jogo aberto direto pela Steam.
  auto with_game = ps;
  with_game.push_back({400, 200, "Hades.exe"});
  std::string name;
  CHECK(FindSteamGame(with_game, &name));
  CHECK(name == "Hades.exe");

  // Jogo que passa por um launcher (neto da Steam).
  auto launcher = ps;
  launcher.push_back({500, 200, "EALauncher.exe"});
  launcher.push_back({501, 500, "Game.exe"});
  CHECK(FindSteamGame(launcher));

  // Jogo aberto fora da Steam (pelo Explorer) não conta.
  auto outside = ps;
  outside.push_back({600, 100, "FortniteClient-Win64-Shipping.exe"});
  CHECK(!FindSteamGame(outside));

  // Pai que já fechou (PID não existe mais) não conta.
  auto orphan = ps;
  orphan.push_back({700, 999, "Algo.exe"});
  CHECK(!FindSteamGame(orphan));

  CHECK(ShouldPause(SteamMode::Auto, true));
  CHECK(!ShouldPause(SteamMode::Auto, false));
  CHECK(ShouldPause(SteamMode::Always, false));
  CHECK(!ShouldPause(SteamMode::Never, true));
}

static void TestSettings() {
  // Padrão sobrevive a ida e volta.
  Settings def;
  CHECK(ParseSettings(SerializeSettings(def)) == def);

  Settings s;
  s.buttons = NintendoLabelMap();
  s.buttons[static_cast<size_t>(Source::ZR)] = Target::A;
  s.profile = "Fortnite";
  s.gyro = GyroMode::RightStick;
  s.mouse_pixels_per_degree = 12.5f;
  s.stick_full_speed_dps = 500;
  s.gyro_invert_y = true;
  s.steam = SteamMode::Never;
  s.home_light = false;
  s.rumble = false;
  s.pc_mode = true;
  s.pc_mouse_speed = 900;
  s.pc_buttons[static_cast<size_t>(Source::A)] = PcAction::AltTab;
  CHECK(ParseSettings(SerializeSettings(s)) == s);
  CHECK(ParseSettings("pc.a = voar\npc.zz = enter\n").pc_buttons == DefaultPcMap());

  // Arquivo editado à mão: comentários, espaços, chave desconhecida e valor
  // inválido não quebram nada.
  Settings h = ParseSettings(
      "# teste\n  giroscopio=mouse  \nlixo = 3\nsensibilidade_mouse = abc\n"
      "luz_home = off # apaga\nsteam = talvez\n");
  CHECK(h.gyro == GyroMode::Mouse);
  CHECK(h.mouse_pixels_per_degree == def.mouse_pixels_per_degree);
  CHECK(!h.home_light);
  CHECK(h.rumble && !ParseSettings("vibracao = nao").rumble);
  CHECK(h.steam == SteamMode::Auto);

  // Valor fora da faixa é ignorado.
  CHECK(ParseSettings("velocidade_analogico = -5").stick_full_speed_dps ==
        def.stick_full_speed_dps);

  // Arquivo antigo com "layout = nome" e um ajuste por cima.
  Settings old = ParseSettings("layout = nome\nbotao.capture = a\nbotao.captura = lb\nbotao.zl = xyz\n");
  CHECK(old.buttons[static_cast<size_t>(Source::A)] == Target::A);
  CHECK(old.buttons[static_cast<size_t>(Source::Capture)] == Target::LB);
  CHECK(old.buttons[static_cast<size_t>(Source::ZL)] == Target::LT);  // valor inválido: fica

  // Perfil: troca botões e giroscópio, mantém Steam e luz.
  Settings cur;
  cur.steam = SteamMode::Never;
  cur.home_light = false;
  ApplyProfile(s, cur);
  CHECK(cur.buttons == s.buttons && cur.gyro == s.gyro && cur.stick_full_speed_dps == 500);
  CHECK(cur.steam == SteamMode::Never && !cur.home_light);
  CHECK(!cur.pc_mode && cur.pc_buttons == DefaultPcMap());  // modo computador é global

  CHECK(SanitizeProfileName("  Jogo: Tiro/2*  ") == "Jogo Tiro2");
  CHECK(SanitizeProfileName("...") == "");
  CHECK(SanitizeProfileName("a.") == "a");

  // Nomes do arquivo existem para todos os botões e voltam iguais.
  for (size_t i = 0; i < static_cast<size_t>(Source::Count); ++i) {
    Source back;
    CHECK(SourceFromId(SourceId(static_cast<Source>(i)), back) && back == static_cast<Source>(i));
  }

  // Níveis de sensibilidade: o padrão é o nível 5, e cada nível é mais rápido.
  CHECK(MouseLevelOf(def.mouse_pixels_per_degree) == 5);
  CHECK(StickLevelOf(def.stick_full_speed_dps) == 5);
  for (int l = 1; l < kGyroLevels; ++l) {
    CHECK(MouseSensForLevel(l + 1) > MouseSensForLevel(l));
    CHECK(StickSpeedForLevel(l + 1) < StickSpeedForLevel(l));  // menos °/s = mais rápido
    CHECK(MouseLevelOf(MouseSensForLevel(l)) == l && StickLevelOf(StickSpeedForLevel(l)) == l);
  }
  CHECK(MouseSensForLevel(99) == MouseSensForLevel(kGyroLevels) && MouseLevelOf(1000) == 10);

  GyroSettings g = ToGyroSettings(s);
  CHECK(g.mode == GyroMode::RightStick && g.stick_full_speed_dps == 500 && g.invert_y);
}

static void TestDesktop() {
  PcMap map = DefaultPcMap();
  DesktopSettings ds;
  DesktopMapper m;
  InputState in;

  // Para cada ação, o id do arquivo volta igual.
  for (size_t i = 0; i < static_cast<size_t>(PcAction::Count); ++i) {
    PcAction back;
    CHECK(PcActionFromId(PcActionId(static_cast<PcAction>(i)), back) &&
          back == static_cast<PcAction>(i));
  }

  // ZL aperta e solta o clique esquerdo.
  in.buttons = BtnZL;
  DesktopFrame f = m.Update(in, map, ds, 0.015f, 0.0);
  CHECK(f.events.size() == 1 && f.events[0] == (PcEvent{PcAction::LeftClick, PcEvent::Down}));
  f = m.Update(in, map, ds, 0.015f, 0.1);
  CHECK(f.events.empty());  // clique não repete
  in.buttons = 0;
  f = m.Update(in, map, ds, 0.015f, 0.2);
  CHECK(f.events.size() == 1 && f.events[0] == (PcEvent{PcAction::LeftClick, PcEvent::Up}));

  // Seta para baixo repete depois de segurar um pouco.
  in.buttons = BtnDown;
  f = m.Update(in, map, ds, 0.015f, 1.0);
  CHECK(f.events.size() == 1 && f.events[0].kind == PcEvent::Down);
  f = m.Update(in, map, ds, 0.015f, 1.2);
  CHECK(f.events.empty());
  f = m.Update(in, map, ds, 0.015f, 1.45);
  CHECK(f.events.size() == 1 && f.events[0] == (PcEvent{PcAction::Down, PcEvent::Repeat}));

  // Trocar o mapa com o botão apertado solta a ação antiga, não a nova.
  PcMap other = map;
  other[static_cast<size_t>(Source::Down)] = PcAction::Enter;
  in.buttons = 0;
  f = m.Update(in, other, ds, 0.015f, 1.5);
  CHECK(f.events.size() == 1 && f.events[0] == (PcEvent{PcAction::Down, PcEvent::Up}));

  // Release solta tudo.
  in.buttons = BtnA | BtnB;
  m.Update(in, map, ds, 0.015f, 2.0);
  f = m.Release();
  CHECK(f.events.size() == 2 && f.events[0].kind == PcEvent::Up && f.events[1].kind == PcEvent::Up);

  // Analógico direito no máximo para a direita e para cima: 1 s = mouse_speed.
  DesktopMapper mm;
  in = InputState{};
  in.rx = 1.0f;
  in.ry = 1.0f;
  int dx = 0, dy = 0;
  for (int i = 0; i < 100; ++i) {
    f = mm.Update(in, map, ds, 0.01f, i * 0.01);
    dx += f.mouse_dx;
    dy += f.mouse_dy;
  }
  CHECK(std::abs(dx - 1600) <= 1 && std::abs(dy + 1600) <= 1);

  // Metade do analógico anda bem menos que a metade (curva), mas anda.
  DesktopMapper mh;
  in.rx = 0.5f;
  in.ry = 0;
  dx = 0;
  for (int i = 0; i < 100; ++i) dx += mh.Update(in, map, ds, 0.01f, i * 0.01).mouse_dx;
  CHECK(dx > 300 && dx < 500);

  // Analógico esquerdo para baixo rola para baixo (rodinha negativa).
  DesktopMapper ms;
  in = InputState{};
  in.ly = -1.0f;
  int wheel = 0;
  for (int i = 0; i < 100; ++i) wheel += ms.Update(in, map, ds, 0.01f, i * 0.01).wheel;
  CHECK(std::abs(wheel + 12 * 120) <= 1);
}

int main() {
  TestRumble();
  TestSticks();
  TestParse();
  TestMapping();
  TestGyroCalibration();
  TestGyroMouse();
  TestGyroStick();
  TestSteam();
  TestSettings();
  TestDesktop();
  if (g_failures == 0) std::printf("Todos os testes passaram.\n");
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
