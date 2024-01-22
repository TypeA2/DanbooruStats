macro(setup_target)
	cmake_parse_arguments(SETUP_TARGET "" "TARGET" "LIBRARIES" ${ARGN})

	target_compile_definitions(${SETUP_TARGET_TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)

	set_target_properties(${SETUP_TARGET_TARGET} PROPERTIES
		CXX_STANDARD 23
		CXX_STANDARD_REQUIRED ON
	)

	target_include_directories(${SETUP_TARGET_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/lib/tqdm-cpp")

	if (MSVC)
		target_compile_options(${SETUP_TARGET_TARGET} PRIVATE /W3)
	else()
		target_compile_options(${SETUP_TARGET_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
	endif()

	target_link_libraries(${SETUP_TARGET_TARGET} PRIVATE ${SETUP_TARGET_LIBRARIES})
endmacro()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)
find_package(SQLiteCpp CONFIG REQUIRED)

add_executable (mariadb_to_sqlite "mariadb_to_sqlite.cpp")
setup_target(TARGET mariadb_to_sqlite LIBRARIES ctre::ctre)


add_executable (database_dump "database_dump.cpp")
setup_target(TARGET database_dump LIBRARIES SQLiteCpp)


add_executable (fast_forward_posts "fast_forward_posts.cpp")
setup_target(TARGET fast_forward_posts LIBRARIES SQLiteCpp)


add_executable (fetch_tags "fetch_tags.cpp")
setup_target(TARGET fetch_tags LIBRARIES
	SQLiteCpp
	nlohmann_json::nlohmann_json
	magic_enum::magic_enum
	OpenSSL::SSL
	OpenSSL::Crypto
	httplib::httplib
)

add_executable(ensure_coherent_post_versions "ensure_coherent_post_versions.cpp")
setup_target(TARGET ensure_coherent_post_versions LIBRARIES
	SQLiteCpp
	nlohmann_json::nlohmann_json
	magic_enum::magic_enum
	OpenSSL::SSL
	OpenSSL::Crypto
	httplib::httplib
)
