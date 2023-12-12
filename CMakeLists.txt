# Ensure that the version of CMake is at least 3.25 (the one we used when developing the project)
cmake_minimum_required(VERSION 3.25)

# Include the SDK
include(pico_sdk_import.cmake)

# Set the project name and langauges used
project(pico_tcp_client C CXX ASM)

# Set C and C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Check that the SDK version is at least 1.5.1 (the one we used when developing the project)
if (PICO_SDK_VERSION_STRING VERSION_LESS "1.5.1")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.5.1 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Set the board to Pico W
set(PICO_BOARD pico_w)

# Initialise the Pico SDK build chain
pico_sdk_init()

# Build the executable plant_monitor from the source files listed
add_executable(pico_tcp_client
  main.c
)

# Specify printf destination
pico_enable_stdio_usb(pico_tcp_client 1)
pico_enable_stdio_uart(pico_tcp_client 0)

# Generate UF2 binary to upload to Pico
pico_add_extra_outputs(pico_tcp_client)

# Link SDK libraries
target_link_libraries(pico_tcp_client pico_stdlib pico_cyw43_arch_lwip_poll)

# Specify folder that CMakeLists.txt is in as an include directory
target_include_directories(pico_tcp_client PRIVATE ${CMAKE_CURRENT_LIST_DIR})