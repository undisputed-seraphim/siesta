if (simdjson_FOUND)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    simdjson
    URL ${CMAKE_SOURCE_DIR}/thirdparty/simdjson-4.6.3.tar.xz
    URL_HASH MD5=867b4de0a7c7d49ad84e429e9e0418ce
)

set(SIMDJSON_BUILD_STATIC_LIB CACHE BOOL ON)
set(SIMDJSON_DISABLE_DEPRECATED_API CACHE BOOL ON)
FetchContent_MakeAvailable(simdjson)
