#ifndef DATABASE_H
#define DATABASE_H

#include <magic_enum.hpp>
#include <magic_enum_containers.hpp>

#include <array>
#include <span>

/* Use unordered set/map implmentations, may be faster */
#define USE_UNORDERED 1

#if USE_UNORDERED
#include <unordered_map>
#include <unordered_set>

template <typename... Args>
using db_map_type = std::unordered_map<Args...>;

template <typename... Args>
using db_set_type = std::unordered_set<Args...>;
#else
#include <map>
#include <set>

template <typename... Args>
using db_map_type = std::map<Args...>;

template <typename... Args>
using db_set_type = std::set<Args...>;
#endif

enum class user_status {
    pending,
    fetching_posts,
    fetching_tags,
    done,
};

enum class tag_type {
    general   = 0,
    artist    = 1,
    copyright = 3,
    character = 4,
    meta      = 5,
};

template <typename T>
using tag_type_array = magic_enum::containers::array<tag_type, T>;

using tag_count_map = db_map_type<std::string_view, int32_t>;

using tag_count = std::pair<std::string_view, int32_t>;

static constexpr auto create_tag_column_index() {
    tag_type_array<int> idx;

    idx[tag_type::general]   = 6;
    idx[tag_type::artist]    = 3;
    idx[tag_type::copyright] = 4;
    idx[tag_type::character] = 5;
    idx[tag_type::meta]      = 7;

    return idx;
}

constexpr auto tag_column_index = create_tag_column_index();

struct post {
    int32_t id;
    int32_t uploader_id;

    tag_type_array<std::string> tags;
};

class database;
class user_stats {
    public:
    private:
    database& _db;

    int32_t _id;

    std::vector<post*> _posts;

    tag_type_array<tag_count_map> _tag_counts;

    public:
    user_stats(const user_stats&) = delete;
    user_stats& operator=(const user_stats&) = delete;

    user_stats(user_stats&&) noexcept = default;
    user_stats& operator=(user_stats&&) noexcept = default;

    explicit user_stats(database& db, int32_t id, std::vector<post*> posts);

    void add_post(post& post);
    [[nodiscard]] std::span<post*> posts();
    [[nodiscard]] const tag_count_map& tag_count(tag_type type);

    private:
    /* Forcibly populate all stats */
    void _populate();

};

class database {
    std::vector<post> _posts;
    db_map_type<int32_t, user_stats> _stats;

    tag_type_array<tag_count_map> _tag_counts;
    tag_type_array<std::vector<tag_count>> _tag_rankings;

    public:
    explicit database(const std::string& path);

    ~database();

    [[nodiscard]] user_stats& stats_for(int32_t id);
    [[nodiscard]] const tag_count_map& tag_counts(tag_type type) const;
    [[nodiscard]] std::span<const tag_count> tag_rankings(tag_type type) const;
};

#endif /* DATABASE_H */
