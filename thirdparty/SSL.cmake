# TODO: Allow user to specify alternative OpenSSL-compatible libraries
# (LibreSSL, WolfSSL, BoringSSL) via a SIESTA_SSL_PROVIDER option or
# by setting OPENSSL_ROOT_DIR to the desired installation prefix.
# All these libraries expose the same C API that Boost.Asio's SSL
# layer expects — the choice is purely a link-time concern.
# When adding provider support, only this file needs to change —
# swap what siesta_ssl links to based on SIESTA_SSL_PROVIDER.
set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL REQUIRED)
set_target_properties(OpenSSL::SSL OpenSSL::Crypto PROPERTIES IMPORTED_GLOBAL TRUE)

add_library(siesta_ssl INTERFACE)
target_link_libraries(siesta_ssl INTERFACE OpenSSL::SSL OpenSSL::Crypto)

# ── Test certificate generation ──────────────────────────────────
find_program(OPENSSL_BINARY openssl REQUIRED)
set(SIESTA_CERT_DIR "${CMAKE_BINARY_DIR}/certs" CACHE PATH "Test TLS certificate directory")

add_custom_command(
	OUTPUT "${SIESTA_CERT_DIR}/server.pem" "${SIESTA_CERT_DIR}/server.key"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${SIESTA_CERT_DIR}"
	COMMAND ${OPENSSL_BINARY} req -x509 -newkey rsa:2048 -nodes
		-keyout "${SIESTA_CERT_DIR}/server.key"
		-out "${SIESTA_CERT_DIR}/server.pem"
		-days 3650 -subj "/CN=localhost"
		-addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
	COMMENT "Generating test TLS certificate"
	VERBATIM
)

add_custom_target(siesta_test_certs
	DEPENDS "${SIESTA_CERT_DIR}/server.pem" "${SIESTA_CERT_DIR}/server.key"
)
