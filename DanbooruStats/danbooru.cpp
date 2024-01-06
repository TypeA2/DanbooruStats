#include "danbooru.h"

#include "util.h"

#include <spdlog/spdlog.h>

danbooru::danbooru(std::string_view username, std::string_view api_key)
    : _username{ username }, _api_key{ api_key }
    , _client{ "https://danbooru.donmai.us" }
    , _rate_limit { 5, std::chrono::seconds(1) } {
    if (username.empty() || api_key.empty()) {
        throw std::runtime_error{ "Username and API key are required" };
    }

    if (!_check_login()) {
        throw std::runtime_error{ "Invalid login credentials" };
    }
}

bool danbooru::user_exists(uint64_t id) {
    return _user_exists(id, nullptr);
}

bool danbooru::user_exists(uint64_t id, std::string& user_name) {
    return _user_exists(id, &user_name);
}

bool danbooru::_check_login() {
    auto res = _get("/profile.json", { { "only", "id,name" } });

    if (!res.contains("id") || !res.contains("name")) {
        spdlog::error("Failed to fetch credentials: {}", res);
        return false;
    }

    spdlog::info("Logging in as {} (user #{})", res["name"], res["id"]);
    return true;
}

web_client::json danbooru::_get(const std::string& path, std::vector<web_client::parameter> params) {
    params.push_back({ "login", _username });
    params.push_back({ "api_key", _api_key });

    return _client.get(path, params);
}

bool danbooru::_user_exists(uint64_t id, std::string* user_name) {
    /* Empty result if we just want to check existence */
    auto res = _get(std::format("/users/{}.json", id), { { "only", (user_name ? "name" : "") } });

    if (res.contains("success") && !res["success"].get<bool>()) {
        spdlog::trace("user #{} does not exist", id);
        return false;
    }

    if (user_name) {
        *user_name = res["name"].get<std::string>();
        spdlog::trace("user #{} exists: {}", id, *user_name);
    } else {
        spdlog::trace("user #{} exists", id);
    }

    return true;
}