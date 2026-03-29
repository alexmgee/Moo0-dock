cmake_minimum_required(VERSION 3.16)
project(moo0-dock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(moo0-dock WIN32 src/main.cpp)

target_link_libraries(moo0-dock PRIVATE
    shell32
    comctl32
    user32
    gdi32
    advapi32
)

# Strip debug info in release for smallest binary
if(MSVC)
    target_compile_options(moo0-dock PRIVATE /W4 /O2)
    set_target_properties(moo0-dock PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup"
    )
    # Embed UAC manifest requesting admin elevation (Moo0 runs elevated, UIPI blocks non-elevated callers)
    target_link_options(moo0-dock PRIVATE /MANIFESTUAC:level=requireAdministrator)
else()
    target_compile_options(moo0-dock PRIVATE -Wall -Wextra -O2)
    target_link_options(moo0-dock PRIVATE -mwindows -static -s)
endif()
