#include <iostream>
#include <fstream>
#include <format>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <ranges>
#include <algorithm>
#include <thread>
#include <memory>

#include <SQLiteCpp/SQLiteCpp.h>
#include <nlohmann/json.hpp>
#include <magic_enum.hpp>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#pragma warning(push)
#pragma warning(disable: 4244)
#include <tqdm.hpp>
#pragma warning(pop)

using std::chrono::steady_clock;
using std::chrono::utc_clock;
using timestamp = utc_clock::time_point;

static void load_dotenv() {
	std::ifstream dotenv { ".env" };

	int err;
	for (std::string line; std::getline(dotenv, line);) {
		if ((err = _putenv(line.c_str()) != 0)) {
			std::println(std::cerr, "Error inserting line \"{}\"", line);
			std::exit(EXIT_FAILURE);
		}
	}

	std::println(std::cerr, "Loaded .env");
}

enum class tag_type : uint8_t {
    general   = 0,
    artist    = 1,
    copyright = 3,
    character = 4,
    meta      = 5,
};

struct tag {
    uint32_t id;
    std::string name;
    uint32_t post_count;
    tag_type category;
    timestamp created_at;
    timestamp updated_at;
    bool is_deprecated;
};

static std::string format_request(const httplib::Params& params) {
    std::stringstream ss;
    std::print(ss, "?");
    if (!params.empty()) {
        for (const auto& [k, v] : params) {
            std::print(ss, "{}={}&", k, v);
        }
    }

    auto str = ss.view();

    return std::string { str.substr(0, str.size() - 1) };
}

static timestamp parse_timestamp(std::string_view sv) {
    std::string date { sv };

    std::stringstream ss { date };
    utc_clock::time_point datetime;
    ss >> std::chrono::parse("%FT%T%Ez", datetime);

    return datetime;
}

static std::string format_timestamp(timestamp time) {
    return std::format("{:%FT%T%Ez}", time);
}

static void process_tags(const std::filesystem::path& db_path, std::string_view username, std::string_view api_key) {
    SQLite::Database db { db_path.string(), SQLite::OPEN_CREATE | SQLite::OPEN_READWRITE };
    db.exec(
        "CREATE TABLE IF NOT EXISTS tags ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "post_count INTEGER NOT NULL,"
        "category INTEGER NOT NULL,"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL,"
        "is_deprecated INTEGER NOT NULL"
        ");"
    );

    /* Sleep for 1 second in between every 10 requests */
    static constexpr size_t rate_limit = 10;

    httplib::Client client { "https://danbooru.donmai.us" };

    std::print(std::cerr, "Fetching latest tag... ");

    uint32_t latest_tag = [&] {
        httplib::Params params;
        params.emplace("login", username);
        params.emplace("api_key", api_key);
        params.emplace("only", "id");
        params.emplace("limit", "1");
        params.emplace("search[hide_empty]", "true");

        auto res = client.Get("/tags.json", params, {});

        if (res->status != 200) {
            throw std::runtime_error { std::format("error: {}", format_request(params)) };
        }

        return nlohmann::json::parse(res->body)[0]["id"];
    }();

    std::println(std::cerr, "{}", latest_tag);

    static constexpr size_t page_size = 1000;

    auto tqdm = tq::trange(latest_tag);

    auto get_tags = [&](uint32_t before, std::span<tag, page_size> buffer) -> std::span<tag> {
        httplib::Params params;
        params.emplace("login", username);
        params.emplace("api_key", api_key);
        params.emplace("only", "id,name,post_count,category,created_at,updated_at,is_deprecated,is_locked");
        params.emplace("limit", std::to_string(page_size));
        params.emplace("search[hide_empty]", "true");
        params.emplace("page", std::format("b{}", before));

        auto res = client.Get("/tags.json", params, {});

        if (res->status != 200) {
            throw std::runtime_error { std::format("error: {}", format_request(params)) };
        }

        auto body = nlohmann::json::parse(res->body);

        auto end = std::ranges::transform(body, buffer.begin(), [](const nlohmann::json& json) {
            return tag {
                .id = json["id"],
                .name = json["name"],
                .post_count = json["post_count"],
                .category = *magic_enum::enum_cast<tag_type>(json["category"].template get<uint32_t>()),
                .created_at = parse_timestamp(json["created_at"]),
                .updated_at = parse_timestamp(json["updated_at"]),
                .is_deprecated = json["is_deprecated"],
            };
        });

        return std::span<tag> { buffer.begin(), end.out };
    };

    uint32_t current_tag = latest_tag + 1;
    auto buffer = std::make_unique<std::array<tag, page_size>>();

    SQLite::Statement query { db, "INSERT INTO tags VALUES (?, ?, ?, ?, ?, ?, ?)" };
    SQLite::Statement begin_transaction { db, "BEGIN TRANSACTION" };
    SQLite::Statement commit { db, "COMMIT" };

    begin_transaction.exec();
    begin_transaction.reset();

    auto bucket_start = steady_clock::now();

    for (size_t i = 0;; ++i) {
        if (i == rate_limit) {
            commit.exec();
            commit.reset();

            /* Sleep every 10 posts, limit to 10 posts per second */
            auto elapsed = steady_clock::now() - bucket_start;

            // std::println(std::cerr, "Took {}", std::chrono::duration_cast<std::chrono::milliseconds>(elapsed));

            i = 0;

            if (elapsed < std::chrono::seconds(1)) {
                std::this_thread::sleep_for(std::chrono::seconds(1) - elapsed);
            }

            begin_transaction.exec();
            begin_transaction.reset();

            bucket_start = steady_clock::now();
        }

        std::span<tag> new_tags = get_tags(current_tag, *buffer);
        
        if (new_tags.empty()) {
            break;
        }
        
        for (const tag& tag : new_tags) {
            if (tag.post_count == 0) {
                std::println(std::cerr, "{} is empty", tag.name);
            }

            query.bind(1, tag.id);
            query.bind(2, tag.name);
            query.bind(3, tag.post_count);
            query.bind(4, magic_enum::enum_integer(tag.category));
            query.bind(5, format_timestamp(tag.created_at));
            query.bind(6, format_timestamp(tag.updated_at));
            query.bind(7, tag.is_deprecated);
            
            int rows = query.exec();

            if (rows != 1) {
                std::println(std::cerr, "Unexpected {} rows modified for tag \"\"", tag.name);
            }

            query.reset();

            tqdm.manually_set_progress(static_cast<double>(latest_tag - tag.id) / latest_tag);
            tqdm.update();
        }
        

        current_tag = new_tags.back().id;
    }

    tqdm.manually_set_progress(1);
    tqdm.update();

    commit.exec();

    std::println(std::cerr, "");
}

int main(int argc, char** argv) {
    std::filesystem::path db_path;
    if (argc != 2) {
        if (argc == 1) {
            std::print(std::cout, "Tags database path: ");
            std::string input;
            std::getline(std::cin, input);
            db_path = input;
        } else {
            std::println(std::cerr, "Usage:\n    {} <tags>", argv[0]);
            return EXIT_FAILURE;
        }
    } else {
        db_path = argv[1];
    }

    load_dotenv();


    if (exists(db_path)) {
        std::print(std::cerr, "\"{}\" already exists, overwrite? (y/n): ", db_path.string());
        std::string response;
        std::getline(std::cin, response);

        if (std::tolower(response.front()) != 'y') {
            std::println(std::cerr, "Exiting.");
            return EXIT_FAILURE;
        }
    }

    try {
        process_tags(db_path, std::getenv("DANBOORU_LOGIN"), std::getenv("DANBOORU_API_KEY"));
    } catch (const std::exception& e) {
        std::print(std::cerr, "Exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

