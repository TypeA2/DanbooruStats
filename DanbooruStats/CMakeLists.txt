add_executable (DanbooruStats "main.cpp"  "web_server.h" "database.h" "database.cpp" "web_server.cpp" "danbooru.h" "danbooru.cpp" "rate_limit.h" "rate_limit.cpp" "web_client.h" "web_client.cpp" "util.h")

target_compile_definitions(DanbooruStats PRIVATE _CRT_SECURE_NO_WARNINGS)

set_target_properties(DanbooruStats PROPERTIES
	CXX_STANDARD 23
	CXX_STANDARD_REQUIRED ON
)

if (MSVC)
	target_compile_options(DanbooruStats PRIVATE /W3)
else()
	target_compile_options(DanbooruStats PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(DanbooruStats PRIVATE OpenSSL::SSL OpenSSL::Crypto)

find_package(httplib CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE httplib::httplib)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE spdlog::spdlog)

find_package(SQLiteCpp CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE SQLiteCpp)

find_package(inja CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE pantor::inja)

find_package(efsw CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE efsw::efsw)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE nlohmann_json::nlohmann_json)

find_package(magic_enum CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE magic_enum::magic_enum)