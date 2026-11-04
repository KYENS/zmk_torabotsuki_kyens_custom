zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

if(CONFIG_SHIELD_TORABO_TSUKI_LP_LEFT OR CONFIG_SHIELD_TORABO_TSUKI_LP_RIGHT)
  zephyr_library()
  zephyr_library_sources(src/board.c src/mini_trackpad_init_reg.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_TP_GESTURES src/tp_gestures.c)
endif()
