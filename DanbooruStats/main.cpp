#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "database.h"
#include "web_server.h"
#include "danbooru.h"
#include "rate_limit.h"

#include <fstream>
#include <string>

static void load_dotenv() {
	std::ifstream dotenv{ ".env" };

	int err;
	for (std::string line; std::getline(dotenv, line);) {
		if ((err = _putenv(line.c_str()) != 0)) {
			spdlog::error("Error inserting line \"{}\"", line);
			std::exit(EXIT_FAILURE);
		}
	}

	spdlog::info("Loaded .env");
}

static void flush_spdlog() {
	spdlog::default_logger()->flush();
}

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
	spdlog::set_level(spdlog::level::trace);
	atexit(flush_spdlog);

	load_dotenv();

	danbooru danbooru { std::getenv("DANBOORU_LOGIN"), std::getenv("DANBOORU_API_KEY") };

	database db { "data/2023.db" };

	web_server server { danbooru, db };

	try {
		server.listen("0.0.0.0", 26980);
	} catch (const std::runtime_error& e) {
		spdlog::error("Fatal error: {}", e.what());
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
