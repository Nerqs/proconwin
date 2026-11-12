cmake_minimum_required(VERSION 3.20)
project(proconwin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PROCON_BUILD_TOOLS "Compila o protótipo de console (precisa do hidapi)" ON)
option(PROCON_BUILD_TESTS "Compila os testes do núcleo" ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Núcleo sem dependência de hardware: protocolo, mapeamento, giroscópio.
add_library(procon_logic STATIC
  core/src/protocol.cpp
  core/src/mapping.cpp
  core/src/gyro.cpp
  core/src/steam.cpp
  core/src/settings.cpp
  core/src/desktop.cpp
)
target_include_directories(procon_logic PUBLIC core/include)

if(PROCON_BUILD_TOOLS)
  # hidapi: baixado automaticamente na primeira compilação.
  include(FetchContent)
  # O hidapi 0.14.0 declara uma versão mínima antiga do CMake, que o CMake 4
  # (já usado no GitHub Actions e em versões novas do Visual Studio) recusa.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG        hidapi-0.14.0
  )
  FetchContent_MakeAvailable(hidapi)

  # Acesso ao controle real via hidapi.
  add_library(procon_device STATIC core/src/controller.cpp)
  target_link_libraries(procon_device PUBLIC procon_logic hidapi::hidapi)

  add_executable(procon_proto tools/procon_proto/main.cpp)
  target_link_libraries(procon_proto PRIVATE procon_device)

  if(WIN32)
    # Motor: Pro Controller -> controle de Xbox 360 virtual (driver
    # ProconWinXusb), giroscópio, Steam e reconexão.
    add_library(procon_engine STATIC
      tools/engine/engine.cpp
      tools/engine/xusb_feeder.cpp
      tools/engine/processes.cpp
      tools/engine/desktop_input.cpp
    )
    target_include_directories(procon_engine PUBLIC tools/engine driver/include)
    target_link_libraries(procon_engine PUBLIC procon_device setupapi user32)

    # Versão de console.
    add_executable(procon_virtual tools/procon_virtual/main.cpp)
    target_link_libraries(procon_virtual PRIVATE procon_engine)

    # Dear ImGui (MIT): desenha a janela do app sobre Direct3D 11.
    FetchContent_Declare(
      imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG        v1.91.5
    )
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    # Sem gamepad no imgui: ele leria pelo XInput o próprio controle virtual.
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS
                               IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
    target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

    # App na bandeja (sem janela de console).
    add_executable(procon_tray WIN32
      tools/procon_tray/main.cpp
      tools/procon_tray/storage.cpp
      tools/procon_tray/labels.cpp
      tools/procon_tray/window.cpp
      tools/procon_tray/ui/app_ui.cpp
      tools/procon_tray/procon_tray.manifest)
    target_link_libraries(procon_tray PRIVATE procon_engine imgui shell32 ole32 advapi32 user32
                          gdi32 comctl32 dwmapi)

    add_executable(procon_install tools/procon_install/main.cpp)
    target_link_libraries(procon_install PRIVATE setupapi newdev shell32)
    if(NOT MSVC)
      target_link_options(procon_install PRIVATE -municode)
    endif()
  endif()
endif()

if(PROCON_BUILD_TESTS)
  enable_testing()
  add_executable(procon_tests tests/test_core.cpp)
  target_link_libraries(procon_tests PRIVATE procon_logic)
  add_test(NAME procon_tests COMMAND procon_tests)
endif()
