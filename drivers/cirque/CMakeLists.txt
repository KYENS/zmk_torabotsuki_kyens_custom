# Cirque Pinnacle driver, vendored from billyjacoby/cirque-input-module@ba53fe8
# (originally petejohanson/cirque-input-module, MIT) with a fix so the
# data-ready interrupt can never stay masked. See README.md.
zephyr_library_named(cirque_pinnacle)
zephyr_library_sources_ifdef(CONFIG_INPUT_PINNACLE input_pinnacle.c)
target_sources_ifdef(CONFIG_ZMK_INPUT_PINNACLE_IDLE_SLEEPER app PRIVATE zmk_pinnacle_idle_sleeper.c)
