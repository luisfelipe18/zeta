cmake_minimum_required(VERSION 3.16)
project(zeta C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler warnings ──────────────────────────────────────────────────
if(MSVC)
    # /W4 warnings; suppress C4996 ("strncpy unsafe") — we use bounded sizes.
    add_compile_options(/W4 /D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# ── Shared source files ───────────────────────────────────────────────
set(COMMON_SRC
    lexer.c
    parser.c
)

# ── zc: full compiler ─────────────────────────────────────────────────
add_executable(zc
    ${COMMON_SRC}
    analyzer.c
    codegen.c
    llvmgen.c
    zc_main.c
)
target_include_directories(zc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# No extra Windows libs needed — we avoided windows.h in platform.h

# ── z_test: AST viewer (developer tool) ──────────────────────────────
add_executable(z_test
    ${COMMON_SRC}
    main.c
)
target_include_directories(z_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ── Install ───────────────────────────────────────────────────────────
install(TARGETS zc RUNTIME DESTINATION bin)

# ── check target: smoke-test all examples ────────────────────────────
# Usage: cmake --build build --target check
if(NOT WIN32)
    set(CHECK_EXAMPLES
        hello fibonacci primes ranges structs list_ops pointers
    )
    set(CHECK_CMDS "")
    foreach(ex IN LISTS CHECK_EXAMPLES)
        list(APPEND CHECK_CMDS
            COMMAND ${CMAKE_COMMAND} -E echo "==> ${ex}.z"
            COMMAND $<TARGET_FILE:zc>
                    ${CMAKE_CURRENT_SOURCE_DIR}/examples/${ex}.z
                    -o /tmp/zc_check_${ex}
            COMMAND /tmp/zc_check_${ex}
        )
    endforeach()
    add_custom_target(check
        DEPENDS zc
        ${CHECK_CMDS}
        COMMAND ${CMAKE_COMMAND} -E echo "All examples passed."
    )
endif()
