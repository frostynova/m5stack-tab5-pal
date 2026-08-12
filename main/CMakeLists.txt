idf_component_register(
    SRCS "app_main.c" "resource_check.c"
    INCLUDE_DIRS "."
    REQUIRES georgik__sdl espressif__m5stack_tab5_noglib sdlpal pthread
)
