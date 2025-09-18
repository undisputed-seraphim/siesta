if (simdjson_FOUND)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    simdjson
    URL ${CMAKE_SOURCE_DIR}/thirdparty/simdjson-4.0.4.tar.xz
    URL_HASH MD5=99086bb5353b60d48329c001912739cd
)

set(SIMDJSON_BUILD_STATIC_LIB CACHE BOOL ON)
set(SIMDJSON_DISABLE_DEPRECATED_API CACHE BOOL ON)
FetchContent_MakeAvailable(simdjson)
