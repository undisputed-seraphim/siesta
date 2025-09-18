if (Catch2_FOUND)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    Catch2
    URL ${CMAKE_SOURCE_DIR}/thirdparty/Catch2-3.10.0.tar.xz
    URL_HASH MD5=45c7a4ff06e8b66c694a269b5f69b1bc
)

set(CATCH_INSTALL_DOCS CACHE BOOL OFF)
set(CATCH_INSTALL_EXTRAS CACHE BOOL OFF)
FetchContent_MakeAvailable(Catch2)
