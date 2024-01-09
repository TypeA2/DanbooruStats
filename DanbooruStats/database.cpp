#include "database.h"

#include "util.h"

#include <ranges>

#include <spdlog/spdlog.h>

using std::chrono::steady_clock;

lazy_stats::lazy_stats(database& db, int32_t id, std::vector<post> posts)
    : _db { db }, _id { id }, _posts { std::move(posts) } {

}

void lazy_stats::populate() {
    auto old_level = spdlog::get_level();
    spdlog::info("Populating user #{}", _id);
    spdlog::set_level(spdlog::level::warn);

    auto begin = steady_clock::now();

    for (tag_type type : magic_enum::enum_values<tag_type>()) {
        (void)tag_count(type);
    }

    auto end = steady_clock::now();

    spdlog::set_level(old_level);
    spdlog::info("  Done ({})", end - begin);
}

std::span<const post> lazy_stats::posts() const {
    return _posts;
}

const lazy_stats::tag_count_map& lazy_stats::tag_count(tag_type type) {
    if (_tag_counts[type].has_value()) {
        return _tag_counts[type].value();
    }

    auto begin = steady_clock::now();

    tag_count_map counts;
    for (const post& post : _posts) {
        for (const auto& tag : post.tags[type] | std::views::split(' ')) {
            std::string_view sv { tag.begin(), tag.end() };

            auto it = counts.find(sv);
            if (it == counts.end()) {
                counts[sv] = 1;
            } else {
                it->second += 1;
            }
        }
    }

    _tag_counts[type] = std::move(counts);

    auto end = steady_clock::now();
    spdlog::info("Calculating {} tag counts took {}", magic_enum::enum_name(type), end - begin);

    return _tag_counts[type].value();
}


database::database(const std::string& path)
#if 0
    : _db{ ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE} {

    {
        auto begin = std::chrono::steady_clock::now();
        _db.backup(path.c_str(), SQLite::Database::Load);
        auto end = std::chrono::steady_clock::now();

        spdlog::info("Loaded database into memory in {}", end - begin);
    }
#else
    : _db { path, SQLite::OPEN_READONLY } {
#endif

    /* Forcibly populate all users, to check max memory usage */
#if 0
    auto begin = steady_clock::now();
    SQLite::Statement distinct_uploaders(_db, "select count(distinct uploader_id) from posts");
    distinct_uploaders.executeStep();
    int32_t user_count = distinct_uploaders.getColumn(0).getInt();

    auto end = steady_clock::now();

    spdlog::info("Fetched all users in {}", end - begin);

    begin = steady_clock::now();

    SQLite::Statement uploaders(_db, "select distinct uploader_id from posts");

    int32_t i = 1;
    while (uploaders.executeStep()) {
        spdlog::info("{} / {} ({} elapsed)", i++, user_count, steady_clock::now() - end);
        stats_for(uploaders.getColumn(0).getInt()).populate();
    }

    end = steady_clock::now();

    spdlog::info("Populated all users in {}", end - begin);
#endif
}

database::~database() {
    
}

lazy_stats& database::stats_for(int32_t id) {
    if (auto it = _stats.find(id); it != _stats.end()) {
        spdlog::info("Fetching data for user #{} from cache", id);

        return it->second;
    }

    spdlog::info("Fetching data for user #{}", id);

    auto begin = steady_clock::now();

    SQLite::Statement query(_db, "select * from posts where uploader_id = ?");
    query.bind(1, id);


    std::vector<post> posts;

    while (query.executeStep()) {

        post post {
            .id = query.getColumn(0).getInt(),
        };

        for (tag_type type : magic_enum::enum_values<tag_type>()) {
            post.tags[type] = query.getColumn(tag_column_index[type]).getString();
        }

        posts.emplace_back(std::move(post));
    }

    auto it = _stats.emplace(id, lazy_stats { *this, id, std::move(posts) });

    auto end = steady_clock::now();
    spdlog::info("Fetching posts took {}", end - begin);

    return it.first->second;
}
