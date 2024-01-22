#include <format>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <variant>
#include <optional>
#include <chrono>
#include <algorithm>
#include <ranges>
#include <sstream>

#pragma warning(push)
#pragma warning(disable: 4244)
#include <tqdm.hpp>
#pragma warning(pop)

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <SQLiteCpp/SQLiteCpp.h>
#include <magic_enum.hpp>
#include <nlohmann/json.hpp>

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

using std::chrono::steady_clock;
using std::chrono::utc_clock;

enum class fetch_type {
    check,
    post,
    version
};

struct post {
    uint32_t version;

    std::vector<uint32_t> pending_versions;

    /* Return whether all versions are processed */
    bool try_apply_pending() {
        /* Check if we can merge pending versions */

        /* There's 19 instances of duplicated version numbers, in which case we treat it as the next one */
        if (pending_versions.size() == 1) {
            uint32_t pending_version = pending_versions.front();
            if (pending_version == version + 1) {
                version = pending_version;
                pending_versions.clear();
                return true;
            } else if (pending_version <= version) {
                version += 1;
                pending_versions.clear();
                return true;
            }

            return false;
        } else if (pending_versions.size() > 1) {
            std::ranges::sort(pending_versions);

            size_t applied_versions = 0;
            for (uint32_t pending_version : pending_versions) {
                if (pending_version == version + 1) {
                    version = pending_version;
                    applied_versions += 1;
                } else if (pending_version <= version) {
                    version += 1;
                    applied_versions += 1;
                } else {
                    break;
                }
            }

            /* Remove the versions we processed */
            pending_versions.erase(pending_versions.begin(), pending_versions.begin() + applied_versions);

            return pending_versions.empty();
        } else {
            return true;
        }
    }
};

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

static std::string reformat_timestamp(std::string_view input) {
    std::stringstream ss { std::string { input } };
    utc_clock::time_point datetime;
    ss >> std::chrono::parse("%FT%T%Ez", datetime);

    return std::format("{:%F %T}", std::chrono::round<std::chrono::seconds>(datetime));
}
template <typename T>
static T value_or_default(const nlohmann::json & val, const T & def) {
    if (val.is_null()) {
        return def;
    } else {
        return val.get<T>();
    }
};

static void process_versions(SQLite::Database& db, fetch_type fetch_by, std::string_view username, std::string_view api_key) {
    std::print(std::cerr, "Finding latest post... ");
    uint32_t latest_post = [&] {
        SQLite::Statement query { db, "SELECT post_id FROM post_versions" };
        uint32_t res = 0;

        auto begin = steady_clock::now();
        while (query.executeStep()) {
            res = std::max<uint32_t>(res, query.getColumn(0));
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(steady_clock::now() - begin);

        std::println(std::cerr, "{}, took {}", res, elapsed);

        return res;
    }();

    std::vector<post> posts(latest_post + 1);

    std::print(std::cerr, "Finding version count... ");
    uint32_t post_versions = [&] {
        auto begin = steady_clock::now();
        uint32_t res = db.execAndGet("SELECT COUNT() FROM post_versions");
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(steady_clock::now() - begin);

        std::println(std::cerr, "{}, took {}", res, elapsed);

        return res;
    }();

    SQLite::Statement query { db, "SELECT id, post_id, version FROM post_versions" };

    std::vector<std::pair<uint32_t, uint32_t>> missing_version_ids;
    size_t total_missing_ids = 0;

    uint32_t prev_id = 1;
    for (auto _ : tq::trange(post_versions)) {
        query.executeStep();
        uint32_t version_id = query.getColumn(0);


        if (version_id != prev_id + 1) {
            missing_version_ids.emplace_back(prev_id + 1, version_id);
            total_missing_ids += (version_id - (prev_id + 1));
        }

        prev_id = version_id;

        post& post = posts[query.getColumn(1).getUInt()];

        uint32_t new_version = query.getColumn(2);
        if (new_version == post.version + 1) {
            post.version = new_version;

            post.try_apply_pending();
        } else if (new_version <= post.version) {
            post.version += 1;
            post.try_apply_pending();
        } else {
            /* Non-sequential versions */
            post.pending_versions.push_back(new_version);
        }
    }

    std::println(std::cerr, "");
    std::println(std::cerr, "Found {} missing version ID ranges, {} total missing IDs", missing_version_ids.size(), total_missing_ids);

    /* Find missing versions */
    size_t posts_with_missing = 0;

    std::vector<std::pair<uint32_t, uint32_t>> missing_versions;
    for (post& post : posts) {
        if (!post.pending_versions.empty()) {

            ++posts_with_missing;

            std::ranges::sort(post.pending_versions);
            uint32_t last_version = post.pending_versions.back();

            for (uint32_t i = post.version + 1; i < last_version; ++i) {
                auto it = std::ranges::find(post.pending_versions, i);
                if (it == post.pending_versions.end()) {
                    missing_versions.emplace_back(static_cast<uint32_t>(std::distance(posts.data(), &post)), i);
                }
            }
        }
    }

    std::println(std::cerr, "Found {} missing versions in {} posts", missing_versions.size(), posts_with_missing);

    if (fetch_by == fetch_type::check) {
        for (const auto& [post, version] : tq::tqdm(missing_versions)) {
            std::println(std::cout, "{},{}", post, version);
        }
        return;
    }

    SQLite::Statement insert_query { db, "INSERT INTO post_versions VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)" };
    httplib::Client client { "https://danbooru.donmai.us" };

    auto join_tags = [](const nlohmann::json& array) -> std::string {
        std::stringstream ss;

        if (!array.empty()) {
            ss << array[0].get<std::string>();

            for (auto it = array.begin() + 1; it != array.end(); ++it) {
                std::print(ss, " {}", it->get<std::string>());
            }
        }

        return ss.str();
    };

    static constexpr size_t page_size = 1000;
    static constexpr size_t rate_limit = 10;

    auto bucket_start = steady_clock::now();

    size_t current_request = 0;

    auto fetch_insert_versions = [&](std::vector<std::pair<std::string, std::string>> kvp) {
        if (current_request == rate_limit) {
            current_request = 0;

            auto elapsed = steady_clock::now() - bucket_start;

            if (elapsed < std::chrono::seconds(1)) {
                std::this_thread::sleep_for(std::chrono::seconds(1) - elapsed);
            }

            bucket_start = steady_clock::now();
        }

        httplib::Params params;
        params.emplace("login", username);
        params.emplace("api_key", api_key);
        params.emplace("only", "id,post_id,added_tags,removed_tags,updater_id,updated_at,rating,rating_changed,parent_id,parent_changed,source,source_changed,version");
        params.emplace("limit", std::to_string(page_size));
        
        for (auto& [k, v] : kvp) {
            params.emplace(std::move(k), std::move(v));
        }

        try {
            auto res = client.Get("/post_versions.json", params, {});

            auto body = nlohmann::json::parse(res->body);

            if (body.type() != nlohmann::json::value_t::array) {
                throw std::runtime_error { res->body };

            }

            for (const auto& version : body) {
                insert_query.bind(1, version["id"].get<uint32_t>());
                insert_query.bind(2, version["post_id"].get<uint32_t>());
                insert_query.bind(3, join_tags(version["added_tags"]));
                insert_query.bind(4, join_tags(version["removed_tags"]));
                insert_query.bind(5, value_or_default<uint32_t>(version["updater_id"], 0));
                insert_query.bind(6, value_or_default<std::string>(version["rating"], ""));
                insert_query.bind(7, value_or_default<bool>(version["rating_changed"], false));
                insert_query.bind(8, value_or_default<uint32_t>(version["parent_id"], 0));
                insert_query.bind(9, value_or_default<bool>(version["parent_changed"], false));
                insert_query.bind(10, value_or_default<std::string>(version["source"], ""));
                insert_query.bind(11, value_or_default<bool>(version["source_changed"], false));
                insert_query.bind(12, version["version"].get<uint32_t>());
                insert_query.bind(13, reformat_timestamp(version["updated_at"].get<std::string>()));

                std::println(std::cout, "{}", insert_query.getExpandedSQL());
                insert_query.exec();
                insert_query.reset();
            }
        } catch (const std::exception& e) {
            std::stringstream ss;
            std::print(ss, "https://danbooru.donmai.us/post_versions.json");
            bool first = true;
            for (const auto& [k, v] : params) {
                if (first) {
                    first = false;
                    std::print(ss, "?{}={}", k, v);
                } else {
                    std::print(ss, "&{}={}", k, v);
                }
            }

            throw std::runtime_error { std::format("{} -> {}", ss.str(), e.what()) };
        }

        std::cout.flush();

        current_request += 1;
    };

    db.exec("BEGIN TRANSACTION");
    if (fetch_by == fetch_type::version) {
        /* Fetch missing version ID ranges */
        auto it = missing_version_ids.begin();

        std::stringstream ss;

        auto tqdm = tq::trange(total_missing_ids);
        uint32_t processed_ids = 1;

        for (; it != missing_version_ids.end();) {
            uint32_t queried_versions = 0;
            uint32_t search_parts = 0;
            while (search_parts < 50 && queried_versions < page_size && it != missing_version_ids.end()) {
                auto& [begin, end] = *it;

                if (end - begin == 1) {
                    std::print(ss, "{},", begin);
                    ++queried_versions;
                    ++search_parts;

                    ++it;

                    processed_ids += 1;
                } else {
                    uint32_t quota_remaining = page_size - queried_versions;

                    uint32_t range_size = end - begin;

                    if (range_size > quota_remaining) {
                        /* Too many, modify range */
                        std::print(ss, "{}...{},", begin, begin + quota_remaining);

                        begin += quota_remaining;

                        processed_ids += quota_remaining;
                    } else {
                        /* Fits */
                        std::print(ss, "{}...{},", begin, end);
                        ++it;

                        processed_ids += range_size;
                    }


                    search_parts += 2;
                }

                tqdm.manually_set_progress(static_cast<double>(processed_ids) / total_missing_ids);
                tqdm.update();
            }

            fetch_insert_versions({ {"search[id]", ss.str() } });
            ss.str("");

        }

    } else {
        /* Fetch missing versions by missing posts versions */

        for (const auto& [post, version] : tq::tqdm(missing_versions)) {
            fetch_insert_versions({
                { "search[post_id]", std::to_string(post) },
                { "search[version]", std::to_string(version) },
            });
        }

    }
    db.exec("COMMIT");
}

int main(int argc, char** argv) {
    std::filesystem::path db_path;
    fetch_type fetch_by;

    if (argc != 3) {
        if (argc == 1) {
            std::string input;
            std::print(std::cout, "Fetch type (check/post/version): ");
            std::getline(std::cin, input);

            auto type = magic_enum::enum_cast<fetch_type>(input, magic_enum::case_insensitive);
            if (!type) {
                std::println(std::cerr, "fetch_by must be \"check\", \"post\" or \"version\"");
                return EXIT_FAILURE;
            }
            
            fetch_by = *type;

            std::print(std::cout, "Tags database path: ");
            std::getline(std::cin, input);
            db_path = input;
        } else {
            std::println(std::cerr, "Usage:\n    {} <fetch_by> <tags>", argv[0]);
            return EXIT_FAILURE;
        }
    } else {
        auto type = magic_enum::enum_cast<fetch_type>(argv[1], magic_enum::case_insensitive);
        if (!type) {
            std::println(std::cerr, "fetch_by must be \"check\", \"post\" or \"version\"");
            return EXIT_FAILURE;
        }

        fetch_by = *type;

        db_path = argv[2];
    }

    if (!exists(db_path)) {
        std::println(std::cerr, "Database does not exist");
        return EXIT_FAILURE;
    }

	load_dotenv();

    try {
        SQLite::Database db { db_path.string(), SQLite::OPEN_READWRITE };
        process_versions(db, fetch_by, std::getenv("DANBOORU_LOGIN"), std::getenv("DANBOORU_API_KEY"));
    } catch (const std::exception& e) {
        std::print(std::cerr, "Exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}