#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <inja/inja.hpp>

#include <efsw/efsw.hpp>

#include <unordered_map>

class web_server : private efsw::FileWatchListener {
    httplib::Server _server;
    std::filesystem::path _template_path;
    inja::Environment _inja;
    efsw::FileWatcher _watcher;
    efsw::WatchID _watch_id;

    enum class template_id {
        users,
    };

    std::unordered_map<template_id, inja::Template> _template_cache;
    std::unordered_map<std::filesystem::path, template_id> _template_paths;

    public:
    virtual ~web_server();

    explicit web_server(const std::string& template_path = "./html/");

    void listen(const std::string& addr, uint16_t port);

    protected:
    virtual void users(int64_t id, const httplib::Request& req, httplib::Response& res);

    private:
    static constexpr std::string_view template_filename(template_id id) {
        using enum template_id;
        switch (id) {
            case users: return "users.html";
        }
    }

    const inja::Template& _ensure_template(template_id id);
    std::string _make_template_path(template_id id) const;

    void handleFileAction(efsw::WatchID watch_id, const std::string& dir,
        const std::string& filename, efsw::Action action, std::string old_filename) override;
};

#endif /* WEB_SERVER_H */