#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "database.h"
#include "web_server.h"

int main() {
	/* Output to stdout and a to a file */
	std::vector<spdlog::sink_ptr> sinks{
		std::make_shared<spdlog::sinks::stdout_color_sink_mt>(),
		std::make_shared<spdlog::sinks::basic_file_sink_mt>("DanbooruStats.log", false),
	};

	auto logger = std::make_shared<spdlog::logger>("default", sinks.begin(), sinks.end());

	/* Always flush on warnings or higher, to ensure log integrity */
	logger->flush_on(spdlog::level::warn);

	/* Format to not print logger name (we don't need it) */
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%^%l%$] [%t] %v");

	spdlog::set_default_logger(logger);

	database db{ "DanbooruStats.db" };
	/*
	httplib::Client cli("https://danbooru.donmai.us");
	cli.set_logger([&](const httplib::Request& req, const httplib::Response& res) {
		spdlog::info("Request: {}{}", cli.host(), req.path);
		spdlog::info("Result: {}", res.status);
	});


	auto res = cli.Get("/users/480070.json");
	spdlog::info("{}\n{}", res->status, res->body);*/

	web_server server;
	server.listen("0.0.0.0", 26980);
	return 0;
}
