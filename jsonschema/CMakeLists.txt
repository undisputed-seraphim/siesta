set(JSONSCHEMA_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/src/schema_types.cpp"
)

add_library(jsonschema STATIC ${JSONSCHEMA_SOURCES})
target_include_directories(jsonschema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(jsonschema PUBLIC 
	simdjson::simdjson_static
)

add_executable(tester main.cpp)
target_link_libraries(tester PRIVATE jsonschema)