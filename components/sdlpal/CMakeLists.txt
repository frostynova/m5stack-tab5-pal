set(SDL_PAL_SOURCES
    "upstream/battle.c"
    "upstream/ending.c"
    "upstream/fight.c"
    "upstream/font.c"
    "upstream/game.c"
    "upstream/global.c"
    "upstream/input.c"
    "upstream/itemmenu.c"
    "upstream/magicmenu.c"
    "upstream/main.c"
    "upstream/map.c"
    "upstream/overlay.c"
    "upstream/palcfg.c"
    "upstream/palcommon.c"
    "upstream/paldebug.c"
    "upstream/palette.c"
    "upstream/play.c"
    "upstream/res.c"
    "upstream/rngplay.c"
    "upstream/scene.c"
    "upstream/script.c"
    "upstream/sound.c"
    "upstream/text.c"
    "upstream/ui.c"
    "upstream/uibattle.c"
    "upstream/uigame.c"
    "upstream/util.c"
    "upstream/video.c"
    "upstream/yj1.c"
    "upstream/resampler.c"
    "upstream/rixplay.cpp"
    "upstream/adplug/binfile.cpp"
    "upstream/adplug/binio.cpp"
    "upstream/adplug/dosbox_opls.cpp"
    "upstream/adplug/emuopls.cpp"
    "upstream/adplug/fprovide.cpp"
    "upstream/adplug/mame_opls.cpp"
    "upstream/adplug/nuked_opl.c"
    "upstream/adplug/player.cpp"
    "upstream/adplug/rix.cpp"
    "upstream/adplug/surroundopl.cpp"
    "upstream/sdl_compat/sdl_compat.c"
    "port/audio_tab5.c"
    "port/pal_utils.c"
    "port/tab5_sdl_touch_bridge.c"
    "port/tab5_touch.c"
)

idf_component_register(
    SRCS ${SDL_PAL_SOURCES}
    INCLUDE_DIRS "port" "upstream" "upstream/sdl_compat"
    REQUIRES georgik__sdl georgik__sdl_bsp esp_psram espressif__m5stack_tab5_noglib esp_codec_dev
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    USE_SDL3=1
    PAL_TAB5=1
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-function
    -Wno-unused-variable
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=ESPIDF_PumpTouchEvent"
    "-Wl,--undefined=__wrap_ESPIDF_PumpTouchEvent"
)
