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