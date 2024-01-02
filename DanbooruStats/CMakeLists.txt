# CMakeList.txt : CMake project for DanbooruStats, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (DanbooruStats "main.cpp"  "web_server.h" "database.h" "database.cpp" "web_server.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET DanbooruStats PROPERTY CXX_STANDARD 20)
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(DanbooruStats PRIVATE OpenSSL::SSL OpenSSL::Crypto)

find_package(httplib CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE httplib::httplib)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE spdlog::spdlog)

find_package(SQLiteCpp REQUIRED)
target_link_libraries(DanbooruStats PRIVATE SQLiteCpp)

find_package(inja CONFIG REQUIRED)
target_link_libraries(DanbooruStats PRIVATE pantor::inja)