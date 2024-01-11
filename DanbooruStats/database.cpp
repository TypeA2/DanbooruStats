#include "database.h"

#include "util.h"

#include <SQLiteCpp/SQLiteCpp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <ranges>
#include <stdexcept>

using std::chrono::steady_clock;

user_stats::user_stats(database& db, int32_t id, std::vector<post*> posts)
    : _db { db }, _id { id }, _posts { std::move(posts) } {
    _populate();
}

void user_stats::add_post(post& post) {
    _posts.push_back(&post);
}

std::span<post*> user_stats::posts() {
    return _posts;
}

const tag_count_map& user_stats::tag_count(tag_type type) {
    return _tag_counts.at(type);
}

void user_stats::_populate() {
    auto old_level = spdlog::get_level();
    spdlog::info("Populating user #{}", _id);
    spdlog::set_level(spdlog::level::warn);

    auto begin = steady_clock::now();

    for (tag_type type : magic_enum::enum_values<tag_type>()) {
        tag_count_map counts;
        for (const post* post_p : _posts) {
            const post& post = *post_p;

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
    }

    auto end = steady_clock::now();

    spdlog::set_level(old_level);
    spdlog::info("  Done ({})", end - begin);
}


database::database(const std::string& path) {
    /* Load entire database into a :memory: database before accessing */
#define USE_MEMORY_BACKUP_DATABASE 0
#if USE_MEMORY_BACKUP_DATABASE
    SQLite::Database db { ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE };

    {
        auto begin = std::chrono::steady_clock::now();
        db.backup(path.c_str(), SQLite::Database::Load);
        auto end = std::chrono::steady_clock::now();

        spdlog::info("Loaded database into memory in {}", end - begin);
    }
#else
    SQLite::Database db { path, SQLite::OPEN_READONLY };
#endif
    auto begin = steady_clock::now();

    SQLite::Statement post_count { db, "select count(*) from posts" };
    post_count.executeStep();

    _posts.resize(post_count.getColumn(0).getInt());

    auto end = steady_clock::now();
    spdlog::info("Allocated {} posts in {}", _posts.size(), end - begin);

    begin = steady_clock::now();

    SQLite::Statement posts { db, "select * from posts" };

    db_map_type<int32_t, std::vector<post*>> user_posts;

    for (size_t i = 0; posts.executeStep(); ++i) {
        post& post = _posts[i];
        post.id = posts.getColumn(0).getInt();
        post.uploader_id = posts.getColumn(1).getInt();

        for (tag_type type : magic_enum::enum_values<tag_type>()) {
            post.tags[type] = posts.getColumn(tag_column_index[type]).getString();
        }

        auto it = user_posts.find(post.uploader_id);
        if (it == user_posts.end()) {
            user_posts.emplace(post.uploader_id, std::vector{ &post });
        } else {
            it->second.push_back(&post);
        }
    }

    end = steady_clock::now();

    spdlog::info("Loaded {} posts in {}", _posts.size(), end - begin);

    auto old_level = spdlog::get_level();
    spdlog::set_level(spdlog::level::warn);

    begin = steady_clock::now();

    for (auto& [uploader_id, posts] : user_posts) {
        /* Count stats for this user */
        user_stats& stats =
            _stats.emplace(uploader_id, user_stats { *this, uploader_id, std::move(user_posts.at(uploader_id)) }).first->second;

        /* Collect stat counts */
        for (tag_type type : magic_enum::enum_values<tag_type>()) {
            const tag_count_map& counts = stats.tag_count(type);

            for (const auto& [tag, count] : counts) {
                tag_count_map& this_counts = _tag_counts[type];
                auto it = this_counts.find(tag);

                if (it == this_counts.end()) {
                    this_counts.emplace(tag, count);
                } else {
                    it->second += count;
                }
            }
        }
    }

    end = steady_clock::now();

    spdlog::set_level(old_level);
    spdlog::info("Calculated tag counts in {}", end - begin);

    begin = steady_clock::now();

    /* Calculate rankings using a maxheap */
    constexpr auto compare_tag_count = [](const tag_count_map::value_type& l, const tag_count_map::value_type& r) -> bool {
        return l.second > r.second;
    };

    for (tag_type type : magic_enum::enum_values<tag_type>()) {

        const tag_count_map& counts = _tag_counts[type];

        std::vector<tag_count> heap;
        heap.reserve(counts.size());

        /* Sort as maxheap */
        for (const auto& tag : counts) {
            heap.emplace_back(tag.first, tag.second);
            std::push_heap(heap.begin(), heap.end(), compare_tag_count);
        }

        /* Convert to ascending order and store */
        std::sort_heap(heap.begin(), heap.end(), compare_tag_count);

        _tag_rankings[type] = std::move(heap);
    }

    end = steady_clock::now();

    spdlog::info("Generates tag rankings in {}", end - begin);

    /* Forcibly populate all users */
#define PREPOPULATE_CACHE 0
#if PREPOPULATE_CACHE
    begin = steady_clock::now();

    db_set_type<int32_t> distinct_uploaders;
    for (const post& post : _posts) {
        distinct_uploaders.insert(post.uploader_id);
    }

    end = steady_clock::now();

    spdlog::info("Fetched {} users in {}", distinct_uploaders.size(), end - begin);

    begin = steady_clock::now();

    int32_t i = 1;
    for (int32_t uploader_id : distinct_uploaders) {
        spdlog::info("{} / {} ({} elapsed)", i++, distinct_uploaders.size(), steady_clock::now() - end);
        stats_for(uploader_id).populate();
    }

    end = steady_clock::now();

    spdlog::info("Populated all users in {}", end - begin);
#endif
}

database::~database() {
    
}

user_stats& database::stats_for(int32_t id) {
    if (auto it = _stats.find(id); it != _stats.end()) {
        spdlog::info("Fetching data for user #{} from cache", id);

        return it->second;
    }
    
    throw std::out_of_range(std::format("user #{} not found", id));
}

const tag_count_map& database::tag_counts(tag_type type) const {
    return _tag_counts.at(type);
}

std::span<const tag_count> database::tag_rankings(tag_type type) const {
    return _tag_rankings.at(type);
}
