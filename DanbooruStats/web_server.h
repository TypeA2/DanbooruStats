#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <inja/inja.hpp>

#include <efsw/efsw.hpp>

#include <unordered_map>

class danbooru;
class database;
class web_server : private efsw::FileWatchListener {
    httplib::Server _server;
    std::filesystem::path _template_path;
    std::filesystem::path _static_path;
    inja::Environment _inja;
    efsw::FileWatcher _watcher;
    efsw::WatchID _watch_id;

    danbooru& _danbooru;
    database& _db;

    enum class template_id {
        user,
        tags,
    };

    std::unordered_map<template_id, inja::Template> _template_cache;
    std::unordered_map<std::filesystem::path, template_id> _template_paths;

    public:
    virtual ~web_server();

    explicit web_server(danbooru& danbooru, database& db, const std::string& template_path = "./html/", const std::string& static_path = "./static/");

    void listen(const std::string& addr, uint16_t port);

    protected:
    virtual void user(int32_t id, const httplib::Request& req, httplib::Response& res);
    virtual void tags(const std::string& category, const httplib::Request& req, httplib::Response& res);

    private:
    [[nodiscard]] static constexpr std::string_view template_filename(template_id id) {
        using enum template_id;
        switch (id) {
            case user:   return "user.html";
            case tags: return "tags.html";
            default: return ""; /* Shouldn't happen */
        }
    }

    [[nodiscard]] const inja::Template& _ensure_template(template_id id);
    [[nodiscard]] std::string _make_template_path(template_id id) const;

    void handleFileAction(efsw::WatchID watch_id, const std::string& dir,
        const std::string& filename, efsw::Action action, std::string old_filename) override;
};

#endif /* WEB_SERVER_H */