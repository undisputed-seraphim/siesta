if (simdjson_FOUND)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    simdjson
    URL ${CMAKE_SOURCE_DIR}/thirdparty/simdjson-4.3.1.tar.xz
    URL_HASH MD5=a887f82618322f5f869bcd1514c4d803
)

set(SIMDJSON_BUILD_STATIC_LIB CACHE BOOL ON)
set(SIMDJSON_DISABLE_DEPRECATED_API CACHE BOOL ON)
FetchContent_MakeAvailable(simdjson)
