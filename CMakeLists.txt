cmake_minimum_required(VERSION 3.28)
project(photo_indexer 
VERSION 0.3.0
DESCRIPTION "Photo Indexer - Metadata Extraction and Indexing Service to FlatBuffer"
HOMEPAGE_URL "https://github.com/Zheng-Bote/photo_indexer"
LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# SIMD
# portable binary:
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -mavx2 -mfma")
# best performance:
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -ffast-math -funroll-loops")


include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake OPTIONAL)

option(USE_AVX2 "Enable AVX2 optimizations" ON)

if(USE_AVX2)
    message(STATUS "AVX2 optimizations enabled")
    add_compile_options(-mavx2 -mfma -O3)
    add_compile_definitions(HAVE_AVX2=1)
else()
    message(STATUS "AVX2 optimizations disabled")
    add_compile_options(-O3)
endif()

find_package(exiv2 REQUIRED)
find_package(flatbuffers REQUIRED)
find_package(xxHash REQUIRED)

add_executable(photo_indexer
    src/main.cpp
)

target_include_directories(photo_indexer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/schemas
)

target_link_libraries(photo_indexer PRIVATE
    exiv2::exiv2
    flatbuffers::flatbuffers
    xxHash::xxhash
)

# FlatBuffers Codegen (optional, kannst du auch manuell machen)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/schemas/metadata_generated.h
    COMMAND flatc --cpp ${CMAKE_CURRENT_SOURCE_DIR}/schemas/metadata.fbs
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/schemas/metadata.fbs
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/schemas
)

add_custom_target(generate_flatbuffers
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/schemas/metadata_generated.h
)

add_dependencies(photo_indexer generate_flatbuffers)

# libs.txt Generate (timing-safe via GENERATE)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/libs.txt" 
     CONTENT "$<TARGET_PROPERTY:photo_indexer,LINK_LIBRARIES>")