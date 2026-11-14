cmake_minimum_required(VERSION 3.16)
project(MyDistributedCache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 当前真正完成的核心库，后续把 LRU 等正式模块加入这里。
add_library(gcache_core STATIC
    src/cache.cpp
)

target_include_directories(gcache_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

target_compile_options(gcache_core PRIVATE
    -Wall -Wextra -Wpedantic
)

enable_testing()

find_package(Threads REQUIRED)

add_executable(test_lru_cache
    tests/test_lru_cache.cpp
)

target_link_libraries(test_lru_cache PRIVATE
    gcache_core
    Threads::Threads
)

add_test(NAME lru_cache COMMAND test_lru_cache)