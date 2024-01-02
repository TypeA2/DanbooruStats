#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <inja/inja.hpp>

#include <unordered_map>

class web_server {
    httplib::Server _server;
    inja::Environment _inja;

    enum class cached_template {
        users,
    };

    std::unordered_map<cached_template, inja::Template> _template_cache;

    public:
    explicit web_server(const std::string& template_path = "./html/");

    void listen(const std::string& addr, uint16_t port);

    protected:
    virtual void users(int64_t id, const httplib::Request& req, httplib::Response& res);

    private:
    const inja::Template& _ensure_template(cached_template id, std::string_view file);
};

#endif /* WEB_SERVER_H */