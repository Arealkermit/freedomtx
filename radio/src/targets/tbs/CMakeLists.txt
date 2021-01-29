option(DISK_CACHE "Enable SD card disk cache" OFF)
option(SHUTDOWN_CONFIRMATION "Shutdown confirmation" OFF)
option(LCD_DUAL_BUFFER "Dual LCD Buffer" OFF)

if (PCB STREQUAL MAMBO OR PCB STREQUAL TANGO)
option(PXX1 "PXX1 protocol support" ON)
option(PXX2 "PXX2 protocol support" OFF)
option(INTERNAL_MODULE_PXX1 "Support for PXX1 internal module" OFF)
option(INTERNAL_MODULE_PXX2 "Support for PXX2 internal module" OFF)
endif()

option(CROSSFIRE_TASK "Crossfire task" YES)
set(PWR_BUTTON "PRESS" CACHE STRING "Pwr button type (PRESS/SWITCH)")
set(STICKS "STANDARD" CACHE STRING "Sticks type (STANDARD/HORUS)")
set(CPU_TYPE STM32F4)
set(CPU_TYPE_FULL STM32F407xE)  # for size report
set(LINKER_SCRIPT targets/tbs/stm32f4_flash.ld)
set(RTC_BACKUP_RAM YES)
set(PPM_UNIT "PPM display unit" PERCENT_PREC0)
set(HAPTIC YES)
set(LUA_EXPORT lua_export_tango)
if (PCB STREQUAL TANGO)
  set(FLAVOUR tango)
  add_definitions(-DSTM32F40_41xxx -DPCBTANGO)
elseif(PCB STREQUAL MAMBO)
  set(FLAVOUR mambo)
  add_definitions(-DSTM32F40_41xxx -DPCBMAMBO)
endif()
set(TBS_RELEASE "Used to build TBS released firmware" ON)
add_definitions(-DEEPROM_SDCARD)
add_definitions(-DEEPROM_VARIANT=0x8000)
add_definitions(-DPWR_BUTTON_${PWR_BUTTON})
add_definitions(-DSOFTWARE_VOLUME)
add_definitions(-DBATT_CRITICAL_SHUTDOWN)

if(DEBUG)
  if(PCB STREQUAL TANGO)
    set(XJT OFF CACHE BOOL "Disable XJT")
    set(MODULE_SIZE_STD OFF CACHE BOOL "Disable MODULE_SIZE_STD")
    set(PPM OFF CACHE BOOL "Disable PPM")
    set(DSM2 OFF CACHE BOOL "Disable DSM2")
    set(SBUS OFF CACHE BOOL "Disable SBUS")
    set(MULTIMODULE OFF CACHE BOOL "Disable MULTIMODULE")
  endif()
  set(AUX_SERIAL_DRIVER ../common/arm/stm32/aux_serial_driver.cpp)
endif()

set(FIRMWARE_TARGET_SRC
  ${FIRMWARE_TARGET_SRC}
  startup_stm32f40_41xxx.s
  )

if (CROSSFIRE_TASK)
  add_definitions(-DCROSSFIRE_TASK)
endif()

add_definitions(-DLIBCRSF_ENABLE_OPENTX_RELATED -DLIBCRSF_ENABLE_SD -DLIBCRSF_ENABLE_COMMAND -DLIBCRSF_ENABLE_PARAMETER)
add_definitions(-DDEFAULT_MODE=2)

set(GUI_DIR 128x64)
set(NAVIGATION_TYPE tango)
set(BITMAPS_TARGET 9x_bitmaps)
set(FONTS_TARGET 9x_fonts_1bit)
set(LCD_DRIVER lcd_driver_spi.cpp)
set(GVAR_SCREEN model_gvars.cpp)

add_definitions(-DESP_SERIAL)
add_definitions(-DENABLE_ROTARY_INVERSE)

add_definitions(-DAGENT)
set(TARGET_SRC
  ${TARGET_SRC}
  ../tbs/usbd_hid_agent.c
  )
  
set(FIRMWARE_TARGET_SRC
  ${FIRMWARE_TARGET_SRC}
  ../../io/crsf/crossfire.cpp
  ../../io/crsf/crsf.cpp
  ../../io/crsf/crsf_write.cpp
  ../../io/crsf/crsf_utilities.cpp
  ../../io/crsf/crc8.cpp
  )

set(HSE_VALUE 8000000)
set(SDCARD YES)
set(EEPROM SDCARD)
set(TARGET_DIR tbs)
set(PPM_LIMITS_SYMETRICAL YES)

set(RADIO_DEPENDENCIES
   ${BITMAPS_TARGET}
   datacopy
   firmware_translations
  )

if(SPLASH STREQUAL OFF)
  message(FATAL_ERROR "Option SPLASH=OFF not supported on TANGO2/MAMBO")
endif()

add_definitions(-DAUDIO -DVOICE -DRTCLOCK)

set(GUI_SRC ${GUI_SRC}
  model_input_edit.cpp
  model_mix_edit.cpp
  model_display.cpp
  radio_diagkeys.cpp
  radio_diaganas.cpp
  view_channels.cpp
  view_telemetry.cpp
  view_about.cpp
  bmp.cpp
  ../screenshot.cpp
  )

if(DISK_CACHE)
  set(SRC ${SRC} disk_cache.cpp)
  add_definitions(-DDISK_CACHE)
endif()

set(TARGET_SRC
  ${TARGET_SRC}
  extmodule_driver.cpp
  ../common/arm/stm32/timers_driver.cpp
  ../common/arm/stm32/audio_dac_driver.cpp
  ../common/arm/stm32/adc_driver.cpp
  ../common/arm/stm32/rotary_encoder_driver.cpp
  ../common/arm/stm32/mixer_scheduler_driver.cpp
  malloc.c
  )

set(FIRMWARE_TARGET_SRC
  ${FIRMWARE_TARGET_SRC}
  ${LCD_DRIVER}
  board.cpp
  backlight_driver.cpp
  ../common/arm/stm32/pwr_driver.cpp
  ../common/arm/stm32/sdio_sd.c
  )

if (PCB STREQUAL TANGO)
  set(FIRMWARE_TARGET_SRC
    ${FIRMWARE_TARGET_SRC}
    led_driver.cpp
    )
endif()

if(LCD_DUAL_BUFFER)
  add_definitions(-DLCD_DUAL_BUFFER)
endif()

set(STM32LIB_SRC
  STM32F4xx_StdPeriph_Driver/src/stm32f4xx_sdio.c
  STM32F4xx_StdPeriph_Driver/src/stm32f4xx_ltdc.c
  STM32F4xx_StdPeriph_Driver/src/stm32f4xx_tim.c
  STM32F4xx_StdPeriph_Driver/src/stm32f4xx_flash.c
  )

set(FIRMWARE_DEPENDENCIES ${FIRMWARE_DEPENDENCIES} ${BITMAPS_TARGET})

if(PYTHONINTERP_FOUND)    
  add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/radio/src/datacopy.cpp
    WORKING_DIRECTORY ${RADIO_DIRECTORY}/src
    COMMAND ${PYTHON_EXECUTABLE} ${RADIO_DIRECTORY}/util/generate_datacopy.py datastructs.h -DPCB${PCB} -DCPUARM -DSTM32F4 -DSTM32F40_41xxx  -DBACKUP -DSIMU -I. -Itargets/tbs -Itargets/common/arm/stm32 -I${THIRDPARTY_DIR} -I${THIRDPARTY_DIR}/STM32F4xx_DSP_StdPeriph_Lib_V1.4.0/Libraries/CMSIS/Device/ST/STM32F4xx/Include ${arm_gcc_include_path} > ${PROJECT_BINARY_DIR}/radio/src/datacopy.cpp
    DEPENDS ${RADIO_DIRECTORY}/src/datastructs.h ${RADIO_DIRECTORY}/util/generate_datacopy.py
    )    
  add_custom_target(datacopy
    DEPENDS ${PROJECT_BINARY_DIR}/radio/src/datacopy.cpp
    )
endif()
