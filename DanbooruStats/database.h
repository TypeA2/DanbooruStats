#ifndef DATABASE_H
#define DATABASE_H

#include <SQLiteCpp/SQLiteCpp.h>
#include <magic_enum.hpp>
#include <magic_enum_containers.hpp>

#include <unordered_map>
#include <array>
#include <span>

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

    tag_type_array<std::string> tags;
};

class database;
class lazy_stats {
    public:
    using tag_count_map = std::unordered_map<std::string_view, int32_t>;

    private:
    database& _db;

    int32_t _id;

    std::vector<post> _posts;

    tag_type_array<std::optional<tag_count_map>> _tag_counts;

    public:
    lazy_stats(const lazy_stats&) = delete;
    lazy_stats& operator=(const lazy_stats&) = delete;

    lazy_stats(lazy_stats&&) noexcept = default;
    lazy_stats& operator=(lazy_stats&&) noexcept = default;

    explicit lazy_stats(database& db, int32_t id, std::vector<post> posts);

    /* Forcibly populate all stats */
    void populate();

    [[nodiscard]] std::span<const post> posts() const;
    [[nodiscard]] const tag_count_map& tag_count(tag_type type);
};

class database {
    SQLite::Database _db;

    std::unordered_map<int64_t, lazy_stats> _stats;

    public:
    explicit database(const std::string& path);

    ~database();

    [[nodiscard]] lazy_stats& stats_for(int32_t id);

    private:

    /* Prepared statements for query reuse */
    enum class query {

    };
    
    std::unordered_map<query, SQLite::Statement> _queries;
};

#endif /* DATABASE_H */
