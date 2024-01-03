#include "web_server.h"

#include <spdlog/spdlog.h>

#include <filesystem>

web_server::~web_server() {
    _watcher.removeWatch(_watch_id);
}

web_server::web_server(const std::string& template_path)
    : _template_path{ std::filesystem::canonical(template_path) }
    , _inja{ template_path /* fs::canonical removes the trailing slash, breaking inja */ } {

    spdlog::info("Loading templates from {}", _template_path.string());

    _watch_id = _watcher.addWatch(_template_path.string(), this, true);
    if (_watch_id < 0) {
        throw std::runtime_error{ std::format("Failed to watch {}, error {}", _template_path.string(), _watch_id) };
    }

    _watcher.watch();

    _server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        spdlog::info("[{}] {} - {}", req.local_addr, req.method, req.path);
    });

    _server.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        spdlog::error("[{}] {} - {}: {}", req.local_addr, req.method, req.path, "Exception:");

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
    
    _server.Get(R"(/users/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        this->users(std::stoll(req.matches[1]), req, res);
    });
}

void web_server::listen(const std::string& addr, uint16_t port) {
    spdlog::info("Listening on {}:{}", addr, port);
    _server.listen(addr, port);
}

void web_server::users(int64_t id, const httplib::Request& req, httplib::Response& res) {
    const auto& tmpl = _ensure_template(template_id::users);
    inja::json data;

    data["user_id"] = id;

    res.set_content(_inja.render(tmpl, data), "text/html");
}

const inja::Template& web_server::_ensure_template(template_id id) {
    /* Return a template by it's ID, load it from a file if not already loaded */
    std::string_view file = template_filename(id);
    auto it = _template_cache.find(id);
    if (it == _template_cache.end()) {
        /* Template does not exist, load and cache it*/
        
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
    switch (action) {
        case efsw::Action::Modified: {
            /* Path relative to template path == template_filename result */
            auto path = canonical(std::filesystem::path(dir) / filename);
            if (is_regular_file(path)) {
                auto it = _template_paths.find(path);
                
                if (it != _template_paths.end()) {
                    spdlog::info("Reloading {}", path.lexically_relative(_template_path).string());

                    /* Reload by just purging the cached template, this avoid reloading unnecessarily */
                    _template_cache.erase(it->second);
                }
            }
            break;
        }

        default: break; /* Ignore for now */
    }
}