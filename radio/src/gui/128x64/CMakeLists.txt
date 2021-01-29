set(GUI_SRC
  ${GUI_SRC}
  lcd.cpp
  splash.cpp
  fonts.cpp
  popups.cpp
  widgets.cpp
  startup_shutdown.cpp
  menu_model.cpp
  model_select.cpp
  model_setup.cpp
  model_failsafe.cpp
  model_logical_switches.cpp
  model_special_functions.cpp
  model_outputs.cpp
  model_telemetry.cpp
  model_telemetry_sensor.cpp
  menu_radio.cpp
  radio_setup.cpp
  radio_calibration.cpp
  view_main.cpp
  view_statistics.cpp
  )

if(PCB STREQUAL TANGO)
  list(REMOVE_ITEM GUI_SRC 
    lcd.cpp
    model_select.cpp
    radio_calibration.cpp)
  set(GUI_SRC
    ${GUI_SRC}
    lcd_4bits.cpp
    radio_calibration_hall.cpp
    model_select_tango.cpp
    )
elseif(PCB STREQUAL MAMBO)
  list(REMOVE_ITEM GUI_SRC 
    model_select.cpp
    radio_calibration.cpp)
  set(GUI_SRC
    ${GUI_SRC}
    radio_calibration_hall.cpp
    model_select_tango.cpp
    )
endif()

if(FLIGHT_MODES)
  set(GUI_SRC
    ${GUI_SRC}
    model_flightmodes.cpp
    )
endif()

if(LUA STREQUAL YES)
  set(GUI_SRC
    ${GUI_SRC}
    model_custom_scripts.cpp
    )
endif()

include(gui/common/stdlcd/CMakeLists.txt)
