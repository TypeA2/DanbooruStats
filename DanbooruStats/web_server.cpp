#include "web_server.h"

#include "danbooru.h"
#include "database.h"
#include "util.h"

#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <queue>
#include <chrono>

using std::chrono::steady_clock;

web_server::~web_server() {
    _watcher.removeWatch(_watch_id);
}

web_server::web_server(danbooru& danbooru, database& db, const std::string& template_path, const std::string& static_path)
    : _template_path{ std::filesystem::canonical(template_path) }
    , _static_path{ std::filesystem::canonical(static_path) }
    , _inja{ template_path /* fs::canonical removes the trailing slash, breaking inja */ }
    , _danbooru { danbooru }, _db { db } {

    spdlog::info("Loading templates from {}", _template_path.string());

    _watch_id = _watcher.addWatch(_template_path.string(), this, true);
    if (_watch_id < 0) {
        throw std::runtime_error{ std::format("Failed to watch {}, error {}", _template_path.string(), _watch_id) };
    }

    _watcher.watch();

    _server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        if (req.has_header("Content-Type")) {
            spdlog::info("[{}] {} - {} ({})", req.remote_addr, req.method, req.path, res.get_header_value("Content-Type"));
        } else {
            spdlog::info("[{}] {} - {}", req.remote_addr, req.method, req.path);
        }
    });

    _server.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        spdlog::error("[{}] {} - {}: {}", req.remote_addr, req.method, req.path, "Exception:");

        constexpr std::string_view fmt = "<h1>Error 500</h1><p>{}</p>";
        std::string body;

        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            body = std::format(fmt, e.what());
            spdlog::error("  {}", e.what());

        } catch (...) {
            body = std::format(fmt, "Unknown error");
            spdlog::error("  Unknown error");
        }

        res.set_content(body, "text/html");
        res.status = 500;
    });

    /* Static files */
    const std::string static_path_string{ _static_path.string() };
    spdlog::info("Serving static files from {}", static_path_string);
    if (!_server.set_mount_point("/static", static_path_string)) {
        throw std::runtime_error{ std::format("Static file directory does not exist: {}", static_path_string) };
    }

    /* Dynamic routing */
    
    _server.Get(R"(/user/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        this->user(std::stoi(req.matches[1]), req, res);
    });

    _server.Get("/tags/([a-z]+)", [this](const httplib::Request& req, httplib::Response& res) {
        this->tags(req.matches[1], req, res);
    });

}

void web_server::listen(const std::string& addr, uint16_t port) {
    spdlog::info("Listening on {}:{}", addr, port);
    _server.listen(addr, port);
}

void web_server::user(int32_t id, const httplib::Request& req, httplib::Response& res) {
    std::string username;
    if (!_danbooru.user_exists(id, username)) {
        res.set_content("User does not exist", "text/html");
        res.status = 404;
        return;
    }

    try {
        const auto& tmpl = _ensure_template(template_id::user);
        inja::json data;

        user_stats& stats = _db.stats_for(id);

        data["user_name"] = username;
        data["user_id"] = id;
        data["posts"] = stats.posts().size();

        data["unique_general_tags"] = stats.tag_count(tag_type::general).size();
        data["unique_artists"] = stats.tag_count(tag_type::artist).size();
        data["unique_characters"] = stats.tag_count(tag_type::character).size();
        data["unique_copyrights"] = stats.tag_count(tag_type::copyright).size();

        res.set_content(_inja.render(tmpl, data), "text/html");
    } catch (const std::out_of_range& e) {
        spdlog::warn("user #{} not found: {}", id, e.what());
        res.set_content(std::format("user #{} not found", id), "text/html");
        res.status = 404;
    }
}

void web_server::tags(const std::string& category, const httplib::Request& req, httplib::Response& res) {
    auto type = magic_enum::enum_cast<tag_type>(category);
    if (!type.has_value()) {
        res.set_content(std::format("category {} not found", category), "text/html");
        res.status = 404;
        return;
    }

    const auto& tmpl = _ensure_template(template_id::tags);

    inja::json data;
    data["tag_type"] = magic_enum::enum_name(type.value());

    static constexpr size_t tag_count = 25;

    data["tag_count"] = tag_count;

    auto tags_array = inja::json::array();

    const auto& counts = _db.tag_rankings(type.value());

    data["total_count"] = counts.size();
    
    for (const auto& tag : counts | std::views::take(tag_count)) {

        tags_array.push_back({ { "tag", tag.first }, { "count", tag.second } });
    }

    data["tags"] = tags_array;

    res.set_content(_inja.render(tmpl, data), "text/html");
}


const inja::Template& web_server::_ensure_template(template_id id) {
    /* Return a template by it's ID, load it from a file if not already loaded */
    std::string_view file = template_filename(id);
    auto it = _template_cache.find(id);
    if (it == _template_cache.end()) {
        /* Template does not exist, load and cache it*/
        spdlog::info("Loading template {}", file);
        _template_paths.emplace(canonical(_template_path / file), id);
        return (_template_cache.emplace(id, _inja.parse_template(std::string{ file })).first)->second;
    }

    return it->second;
}

std::string web_server::_make_template_path(template_id id) const {
    return canonical(_template_path / template_filename(id)).string();
}

void web_server::handleFileAction(efsw::WatchID watch_id, const std::string& dir,
    const std::string& filename, efsw::Action action, std::string old_filename)  {
    /* We always handle events by purging cache, so removing the file is handled too */

    /* If path is base.html, reload everything */
    if (filename == "base.html") {
        spdlog::info("Purging template cache");
        _template_cache.clear();
        return;
    }

    /* Path relative to template path == template_filename result */
    auto path = std::filesystem::path(dir) / filename;
    try {
        if (is_regular_file(path)) {
            path = canonical(path);
            auto it = _template_paths.find(path);

            if (it != _template_paths.end()) {
                spdlog::info("Reloading {}", path.lexically_relative(_template_path).string());

                /* Reload by just purging the cached template, this avoid reloading unnecessarily */
                _template_cache.erase(it->second);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        /* err, what? */
        spdlog::error("efsw watcher error: {}", dir, filename, e.what());
    }
}