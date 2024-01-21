add_executable (mariadb_to_sqlite "mariadb_to_sqlite.cpp")

target_compile_definitions(mariadb_to_sqlite PRIVATE _CRT_SECURE_NO_WARNINGS)

set_target_properties(mariadb_to_sqlite PROPERTIES
	CXX_STANDARD 23
	CXX_STANDARD_REQUIRED ON
)

if (MSVC)
	target_compile_options(mariadb_to_sqlite PRIVATE /W3)
else()
	target_compile_options(mariadb_to_sqlite PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(ctre CONFIG REQUIRED)
target_link_libraries(mariadb_to_sqlite PRIVATE ctre::ctre)


add_executable (database_dump "database_dump.cpp")

target_compile_definitions(database_dump PRIVATE _CRT_SECURE_NO_WARNINGS)

set_target_properties(database_dump PROPERTIES
	CXX_STANDARD 23
	CXX_STANDARD_REQUIRED ON
)

if (MSVC)
	target_compile_options(database_dump PRIVATE /W3)
else()
	target_compile_options(database_dump PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(SQLiteCpp CONFIG REQUIRED)
target_link_libraries(database_dump PRIVATE SQLiteCpp)

add_executable (fast_forward_posts "fast_forward_posts.cpp")

target_compile_definitions(fast_forward_posts PRIVATE _CRT_SECURE_NO_WARNINGS)

set_target_properties(fast_forward_posts PROPERTIES
	CXX_STANDARD 23
	CXX_STANDARD_REQUIRED ON
)

if (MSVC)
	target_compile_options(fast_forward_posts PRIVATE /W3)
else()
	target_compile_options(fast_forward_posts PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(fast_forward_posts PRIVATE SQLiteCpp)

target_include_directories(fast_forward_posts PRIVATE "${PROJECT_SOURCE_DIR}/lib/tqdm-cpp")

add_executable (fetch_tags "fetch_tags.cpp")

target_compile_definitions(fetch_tags PRIVATE _CRT_SECURE_NO_WARNINGS)

set_target_properties(fetch_tags PROPERTIES
	CXX_STANDARD 23
	CXX_STANDARD_REQUIRED ON
)

if (MSVC)
	target_compile_options(fetch_tags PRIVATE /W3)
else()
	target_compile_options(fetch_tags PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(fetch_tags PRIVATE SQLiteCpp)

target_include_directories(fetch_tags PRIVATE "${PROJECT_SOURCE_DIR}/lib/tqdm-cpp")

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(fetch_tags PRIVATE nlohmann_json::nlohmann_json)

find_package(magic_enum CONFIG REQUIRED)
target_link_libraries(fetch_tags PRIVATE magic_enum::magic_enum)

find_package(OpenSSL REQUIRED)
target_link_libraries(fetch_tags PRIVATE OpenSSL::SSL OpenSSL::Crypto)

find_package(httplib CONFIG REQUIRED)
target_link_libraries(fetch_tags PRIVATE httplib::httplib)