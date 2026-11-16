cmake_minimum_required(VERSION 3.16)
project(MyDistributedCache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 核心库
add_library(gcache_core STATIC
    src/cache.cpp
    src/singleflight.cpp
    src/consistent_hash.cpp
)

target_include_directories(gcache_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

target_compile_options(gcache_core PRIVATE
    -Wall -Wextra -Wpedantic
)

enable_testing()
find_package(Threads REQUIRED)

# ---- 测试 1：LRU cache ----
add_executable(test_lru_cache
    tests/test_lru_cache.cpp
)
target_link_libraries(test_lru_cache PRIVATE
    gcache_core                    # 修正拼写 k -> g
    Threads::Threads               # 若用到线程
)
target_compile_options(test_lru_cache PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME lru_cache COMMAND test_lru_cache)

# ---- 测试 2：consistent hash ----
add_executable(test_consistent_hash
    tests/test_consistent_hash.cpp
)
target_link_libraries(test_consistent_hash PRIVATE
    gcache_core
    Threads::Threads
)
target_compile_options(test_consistent_hash PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME consistent_hash COMMAND test_consistent_hash)