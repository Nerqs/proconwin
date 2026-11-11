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
    # Cliente do driver de controle virtual (ViGEmClient, licença MIT).
    add_library(vigemclient STATIC third_party/vigemclient/src/ViGEmClient.cpp)
    target_include_directories(vigemclient PUBLIC third_party/vigemclient/include)
    target_link_libraries(vigemclient PUBLIC setupapi)
    # Código de terceiros: compila sem o nível de avisos do projeto.
    if(MSVC)
      set_target_properties(vigemclient PROPERTIES COMPILE_OPTIONS "/utf-8;/W1")
    else()
      set_target_properties(vigemclient PROPERTIES COMPILE_OPTIONS "-w")
    endif()

    add_executable(procon_virtual tools/procon_virtual/main.cpp)
    target_link_libraries(procon_virtual PRIVATE procon_device vigemclient)

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
