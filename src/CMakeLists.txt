file(GLOB LIB_SOURCES "*.cc")

add_library(imp_debug STATIC ${LIB_SOURCES})

add_library(imp_sanitized EXCLUDE_FROM_ALL STATIC ${LIB_SOURCES})
target_compile_options(imp_sanitized PUBLIC ${SANITIZING_FLAGS})

add_library(imp_optimized EXCLUDE_FROM_ALL STATIC ${LIB_SOURCES})
target_compile_options(imp_optimized PUBLIC -O2 -DNDEBUG)
