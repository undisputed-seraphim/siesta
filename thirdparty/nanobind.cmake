if (nanobind_FOUND)
    return()
endif()

include(FetchContent)

set(NB_TEST OFF CACHE BOOL "" FORCE)
set(NB_USE_SUBMODULE_DEPS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nanobind
    URL ${CMAKE_SOURCE_DIR}/thirdparty/nanobind-2.12.0.tar.xz
    URL_HASH MD5=e5c67363b00440d0af5ff2394c7f4933
)

FetchContent_MakeAvailable(nanobind)
