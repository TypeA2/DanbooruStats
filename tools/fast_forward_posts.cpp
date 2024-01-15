#include <iostream>
#include <format>
#include <filesystem>
#include <vector>
#include <unordered_set>
#include <array>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <chrono>
#include <map>
#include <string_view>
#include <ranges>
#include <concepts>

#include "progressbar.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

template <> struct std::formatter<std::chrono::nanoseconds> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const std::chrono::nanoseconds& ns, format_context& ctx) const {
        if (ns < std::chrono::microseconds(1)) {
            return std::format_to(ctx.out(), "{} ns", ns.count());
        } else if (ns < std::chrono::milliseconds(1)) {
            return std::format_to(ctx.out(), "{:.3f} us", ns.count() / 1e3);
        } else if (ns < std::chrono::seconds(1)) {
            return std::format_to(ctx.out(), "{:.3f} ms", ns.count() / 1e6);
        }

        return std::format_to(ctx.out(), "{:.3f} s", ns.count() / 1e9);
    }
};

struct format_bytes {
    format_bytes(size_t count) : count { count } { }
    size_t count;
};

template <> struct std::formatter<format_bytes> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const format_bytes& bytes, format_context& ctx) const {
        if (bytes.count < 1024) {
            return std::format_to(ctx.out(), "{} bytes", bytes.count);
        } else if (bytes.count < size_t { 1024 } *1024) {
            return std::format_to(ctx.out(), "{:.3f} KiB", bytes.count / 1024.);
        } else if (bytes.count < size_t { 1024 } *1024 * 1024) {
            return std::format_to(ctx.out(), "{:.3f} MiB", bytes.count / 1024. / 1024.);
        } else if (bytes.count < size_t { 1024 } *1024 * 1024 * 1024) {
            return std::format_to(ctx.out(), "{:.3f} GiB", bytes.count / 1024. / 1024. / 1024.);
        }

        return std::format_to(ctx.out(), "{:.3f} TiB", bytes.count / 1024. / 1024. / 1024. / 1024.);
    }
};

using std::chrono::steady_clock;

//using tag_set = std::unordered_set<uint32_t>;
using tag_set = std::vector<uint32_t>;

struct post {
    uint32_t id;
    uint32_t uploader_id;
    uint32_t approver_id;

    tag_set tag_string_artist;
    tag_set tag_string_copyright;
    tag_set tag_string_character;
    tag_set tag_string_general;
    tag_set tag_string_meta;

    char rating;

    uint32_t parent_id;
    
    std::string source;
    
    std::optional<std::array<char, 32>> md5;
    std::array<char, 4> file_ext;
    uint32_t file_size;

    uint16_t image_height;
    uint16_t image_width;

    int32_t score;
    uint32_t fav_count;
    uint16_t tag_count;
    uint16_t tag_count_general;
    uint16_t tag_count_artist;
    uint16_t tag_count_copyright;
    uint16_t tag_count_character;
    uint16_t tag_count_meta;

    bool has_children;
    int32_t up_score;
    int32_t down_score;
    bool is_deleted;
    bool is_banned;

    std::optional<std::string> last_commented_at;
    std::optional<std::string> last_comment_bumped_at;
    std::optional<std::string> last_noted_at;
    std::string created_at;
    std::string updated_at;
};

template <size_t N>
[[nodiscard]] std::array<char, N> string_as_array(std::string_view sv) {
    std::array<char, N> result;

    std::copy_n(sv.begin(), N, result.begin());

    return result;
}

template <size_t N>
std::optional<std::array<char, N>> optional_string_as_array(const SQLite::Column& col) {
    if (col.isNull()) {
        return {};
    } else {
        return string_as_array<N>(col.getText());
    }
}

[[nodiscard]] static std::optional<std::string> optional_string(const SQLite::Column& col) {
    if (col.isNull()) {
        return {};
    } else {
        return col.getString();
    }
}

struct posts_database {
    struct string_hasher {
        using hash_type = std::hash<std::string_view>;
        using is_transparent = void;

        template <std::ranges::sized_range T>
        std::size_t operator()(const T& str) const { return hash_type {}({ std::begin(str), std::end(str) }); }
        std::size_t operator()(const char* str) const { return hash_type {}(str); }
        std::size_t operator()(std::string_view str) const { return hash_type {}(str); }
        std::size_t operator()(const std::string& str) const { return hash_type {}(str); }
    };

    struct range_eq : std::equal_to<> {
        template <typename T1, typename T2> requires std::ranges::sized_range<T1> || std::ranges::sized_range<T2>
        [[nodiscard]] constexpr auto operator()(T1 && lhs, T2 && rhs) const
            noexcept(noexcept(std::ranges::equal(std::forward<T1>(lhs), std::forward<T2>(rhs))))
            -> decltype(std::ranges::equal(std::forward<T1>(lhs), std::forward<T2>(rhs))) {
            return std::ranges::equal(std::forward<T1>(lhs), std::forward<T2>(rhs));
        }
    };

    std::unordered_map<std::string, uint32_t, string_hasher, range_eq> tags;
    std::vector<post> posts;
    size_t total_tags = 0;

    posts_database(const std::filesystem::path& path) {
        SQLite::Database db { path.string() };
        std::print(std::cerr, "Retrieving post count...");
        int post_count = db.execAndGet("SELECT COUNT() FROM posts");
        posts.reserve(post_count);
        std::println(std::cerr, " {}", post_count);

        SQLite::Statement query { db, "SELECT * FROM posts" };

        progressbar progress(post_count / 10000);
        progress.set_done_char("█");

        size_t i = 0;

        uint16_t tag_count_general;
        uint16_t tag_count_artist;
        uint16_t tag_count_copyright;
        uint16_t tag_count_character;
        uint16_t tag_count_meta;
        while (query.executeStep()) {
            post post {
                .id                     = query.getColumn(0),
                .uploader_id            = query.getColumn(1),
                .approver_id            = query.getColumn(2),
                .tag_string_artist      = split_tags(query.getColumn(3).getText(), (tag_count_artist = query.getColumn(20))),
                .tag_string_copyright   = split_tags(query.getColumn(4).getText(), (tag_count_copyright = query.getColumn(21))),
                .tag_string_character   = split_tags(query.getColumn(5).getText(), (tag_count_character = query.getColumn(22))),
                .tag_string_general     = split_tags(query.getColumn(6).getText(), (tag_count_general = query.getColumn(19))),
                .tag_string_meta        = split_tags(query.getColumn(7).getText(), (tag_count_meta = query.getColumn(23))),
                .rating                 = *query.getColumn(8).getText(),
                .parent_id              = query.getColumn(9),
                .source                 = query.getColumn(10).getString(),
                .md5                    = optional_string_as_array<32>(query.getColumn(11)),
                .file_ext               = string_as_array<4>(query.getColumn(12).getText()),
                .file_size              = query.getColumn(13),
                .image_height           = query.getColumn(14),
                .image_width            = query.getColumn(15),
                .score                  = query.getColumn(16),
                .fav_count              = query.getColumn(17),
                .tag_count              = query.getColumn(18),
                .tag_count_general      = tag_count_general,
                .tag_count_artist       = tag_count_artist,
                .tag_count_copyright    = tag_count_copyright,
                .tag_count_character    = tag_count_character,
                .tag_count_meta         = tag_count_meta,
                .has_children           = query.getColumn(24).getUInt() ? true : false,
                .up_score               = query.getColumn(25),
                .down_score             = query.getColumn(26),
                .is_deleted             = query.getColumn(27).getUInt() ? true : false,
                .is_banned              = query.getColumn(28).getUInt() ? true : false,
                .last_commented_at      = optional_string(query.getColumn(29)),
                .last_comment_bumped_at = optional_string(query.getColumn(30)),
                .last_noted_at          = optional_string(query.getColumn(31)),
                .created_at             = query.getColumn(32).getString(),
                .updated_at             = query.getColumn(33).getString(),
            };

            posts.emplace_back(std::move(post));

            if (i % 10000 == 0) {
                progress.update();
            }

            ++i;
        }

    }

    void sanity_check() const {
        for (const post& post : posts) {
            if (post.tag_count_general != post.tag_string_general.size()) {
                //throw std::runtime_error { std::format("post #{} general: {} != {}", post.id, post.tag_count_general, post.tag_string_general.size()) };
                std::println(std::cout, "post #{} general: {} != {}", post.id, post.tag_count_general, post.tag_string_general.size());
            }

            if (post.tag_count_copyright != post.tag_string_copyright.size()) {
                //throw std::runtime_error { std::format("post #{} copyright: {} != {}", post.id, post.tag_count_copyright, post.tag_string_copyright.size()) };
                std::println(std::cout, "post #{} copyright: {} != {}", post.id, post.tag_count_copyright, post.tag_string_copyright.size());
            }

            if (post.tag_count_artist != post.tag_string_artist.size()) {
                //throw std::runtime_error { std::format("post #{} artist: {} != {}", post.id, post.tag_count_artist, post.tag_string_artist.size()) };
                std::println(std::cout, "post #{} artist: {} != {}", post.id, post.tag_count_artist, post.tag_string_artist.size());
            }

            if (post.tag_count_character != post.tag_string_character.size()) {
                //throw std::runtime_error { std::format("post #{} character: {} != {}", post.id, post.tag_count_character, post.tag_string_character.size()) };
                std::println(std::cout, "post #{} character: {} != {}", post.id, post.tag_count_character, post.tag_string_character.size());
            }

            if (post.tag_count_meta != post.tag_string_meta.size()) {
                //throw std::runtime_error { std::format("post #{} meta: {} != {}", post.id, post.tag_count_meta, post.tag_string_meta.size()) };
                std::println(std::cout, "post #{} meta: {} != {}", post.id, post.tag_count_meta, post.tag_string_meta.size());
            }

        }
    }

    private:
    uint32_t _current_tag = 0;

    [[nodiscard]] tag_set split_tags(std::string_view string, uint16_t reserve = 0) {
        tag_set result;
        result.reserve(reserve);

        for (auto tag : string | std::views::split(' ')) {
            auto it = tags.find(tag);
            if (it == tags.end()) {
                tags.emplace(std::string { tag.begin(), tag.end() }, _current_tag);

                /* First occurence of this tag, so it can never be already present */
                result.push_back(_current_tag);
                std::ranges::push_heap(result);

                ++_current_tag;
            } else {
                auto it2 = std::ranges::find(result, it->second);
                if (it2 == result.end()) {
                    result.push_back(it->second);
                }
            }

            ++total_tags;
        }

        return result;
    }
};

void process_posts(const std::filesystem::path& posts_path, const std::filesystem::path& posts_versions_path, const std::filesystem::path& output_path);

int main(int argc, char** argv) {
    if (argc != 4) {
        std::println(std::cerr, "Usage:\n    {} <posts> <post_versions> <output>", argv[0]);
        return EXIT_FAILURE;
    }

    std::filesystem::path posts = argv[1];
    std::filesystem::path post_versions = argv[2];
    std::filesystem::path output = argv[3];

    if (!exists(posts)) {
        std::println(std::cerr, "\"{}\" does not exist", argv[1]);
        return EXIT_FAILURE;
    }

    if (!exists(post_versions)) {
        std::println(std::cerr, "\"{}\" does not exist", argv[2]);
        return EXIT_FAILURE;
    }

    if (exists(output)) {
        std::print(std::cerr, "\"{}\" already exists, overwrite? (y/n): ", argv[3]);
        std::string response;
        std::getline(std::cin, response);

        if (std::tolower(response.front()) != 'y') {
            std::println(std::cerr, "Exiting.");
            return EXIT_FAILURE;
        }
    }

    try {
        process_posts(posts, post_versions, output);
    } catch (const std::exception& e) {
        std::print(std::cerr, "Exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void process_posts(const std::filesystem::path& posts_path, const std::filesystem::path& posts_versions_path, const std::filesystem::path& output_path) {
    auto begin = steady_clock::now();
    posts_database posts { posts_path };
    auto elapsed = steady_clock::now() - begin;

    std::println (std::cerr, "");

    double seconds = elapsed.count() / 1e9;
    size_t tags_per_second = static_cast<size_t>(std::round(posts.tags.size() / seconds));
    size_t total_tags_per_second = static_cast<size_t>(std::round(posts.total_tags / seconds));
    size_t posts_per_second = static_cast<size_t>(std::round(posts.posts.size() / seconds));

    // posts.sanity_check();

    std::println(std::cerr, "Loaded posts in {}:", elapsed);
    std::println(std::cerr, "  {} posts", posts.posts.size());
    std::println(std::cerr, "    {}/s", posts_per_second);
    std::println(std::cerr, "  {} unique tags", posts.tags.size());
    std::println(std::cerr, "    {}/s", tags_per_second);
    std::println(std::cerr, "  {} total tags", posts.total_tags);
    std::println(std::cerr, "    {}/s", total_tags_per_second);

    std::string s;
    std::cin >> s;
}
