#include "web_server.h"

#include <spdlog/spdlog.h>

#include <filesystem>

web_server::web_server(const std::string& template_path)
    : _inja{ template_path } {

    spdlog::info("Loading templates from {}", canonical(std::filesystem::path(template_path)).string());

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
    const auto& tmpl = _ensure_template(cached_template::users, "users.html");
    inja::json data;

    data["user_id"] = id;

    res.set_content(_inja.render(tmpl, data), "text/html");
}

const inja::Template& web_server::_ensure_template(cached_template id, std::string_view file) {
    /* Return a template by it's ID, load it from a file if not already loaded */
    auto it = _template_cache.find(id);
    if (it == _template_cache.end()) {
        return (_template_cache.emplace(id, _inja.parse_template(std::string{ file })).first)->second;
    }

    return it->second;
}