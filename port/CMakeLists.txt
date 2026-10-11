cmake_minimum_required(VERSION 3.13)
project(KnightOfKnights C)

set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

file(GLOB CORE_SRC
  src/core/*.c src/fmt/*.c src/rm/*.c src/dx/*.c src/game/*.c src/game/gen/*.c)

if(PSP)
  file(GLOB PLAT_SRC src/plat/psp/*.c)
  add_executable(kok ${CORE_SRC} ${PLAT_SRC} src/main.c)
  # -O2 on purpose (Release adds -O3 first): the generated code is large and the Allegrex has a
  # 16 KB I-cache. No -ffast-math: NAN() and the x87 compare emulation need IEEE semantics.
  target_compile_options(kok PRIVATE -O2 -G0 -Wall -Wno-unused-variable -fno-strict-aliasing
    -fno-math-errno -mno-check-zero-division -ffunction-sections -fdata-sections)
  target_link_options(kok PRIVATE -Wl,--gc-sections)
  target_link_libraries(kok PRIVATE
    pspgum pspgu pspctrl pspaudio pspdisplay pspge psprtc psppower pspdebug m)
  # psp/ICON0.PNG and psp/PIC1.PNG are made from the game's title screen by tools/make_data.sh
  set(KOK_ICON NULL)
  set(KOK_PIC1 NULL)
  if(EXISTS ${CMAKE_SOURCE_DIR}/psp/ICON0.PNG)
    set(KOK_ICON ${CMAKE_SOURCE_DIR}/psp/ICON0.PNG)
  endif()
  if(EXISTS ${CMAKE_SOURCE_DIR}/psp/PIC1.PNG)
    set(KOK_PIC1 ${CMAKE_SOURCE_DIR}/psp/PIC1.PNG)
  endif()
  create_pbp_file(
    TARGET kok
    TITLE "Knight of Knights"
    ICON_PATH ${KOK_ICON}
    BACKGROUND_PATH ${KOK_PIC1}
    PREVIEW_PATH NULL
    MEMSIZE 1
    BUILD_PRX
  )
else()
  find_package(SDL2 REQUIRED)
  find_package(OpenGL REQUIRED)
  file(GLOB PLAT_SRC src/plat/pc/*.c)
  add_executable(kok ${CORE_SRC} ${PLAT_SRC} src/main.c)
  target_compile_options(kok PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
    -Wno-misleading-indentation -fno-strict-aliasing)
  target_link_libraries(kok PRIVATE SDL2::SDL2 OpenGL::GL m)
  target_link_options(kok PRIVATE -rdynamic)
  add_executable(xview ${CORE_SRC} ${PLAT_SRC} tools/xview.c)
  target_compile_options(xview PRIVATE -fno-strict-aliasing)
  target_link_libraries(xview PRIVATE SDL2::SDL2 OpenGL::GL m)
endif()
target_include_directories(kok PRIVATE src third_party)
if(TARGET xview)
  target_include_directories(xview PRIVATE src third_party)
endif()
