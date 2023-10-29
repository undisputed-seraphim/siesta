add_executable(echo_client
	"client.cpp"
	#"echo_client.cpp"
	"echo_client.hpp"
	"echo_defs.cpp"
	"echo_defs.hpp"
)
target_include_directories(echo_client PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(echo_client
	PRIVATE
	siesta
)

add_executable(echo_server
	"server.cpp"
	"echo_server.cpp"
	"echo_server.hpp"
	"echo_defs.cpp"
	"echo_defs.hpp"
)
target_include_directories(echo_server PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(echo_server
	PRIVATE
	siesta
)
