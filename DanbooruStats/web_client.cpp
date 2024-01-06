#include "web_client.h"

#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

#include <sstream>

web_client_exception::web_client_exception(std::string_view method, int status, std::string_view msg)
    : runtime_error { std::format("{}: {} - {}", method, status, msg) } {

}

web_client::web_client(const std::string& url, size_t bucket_size, rate_limit::duration refill_delay)
    : _client { url }
    , _use_rate_limit { bucket_size != -1 }, _rate_limit { bucket_size, refill_delay } {

}

web_client::json web_client::get(const std::string& path, std::vector<parameter> params) {
    httplib::Params client_params;
    for (const parameter& p : params) {
        client_params.insert({ std::string { p.first }, std::string { p.second }});
    }
    
    _acquire();
    httplib::Result res = _client.Get(path, client_params, {});

    if (res->status != 200) {
        std::stringstream ss;
        fmt::print(ss, "{}", path);
        if (!params.empty()) {
            fmt::print(ss, "?{}={}", params.front().first, params.front().second);

            for (size_t i = 1; i < params.size(); ++i) {
                fmt::print(ss, "&{}={}", params[i].first, params[i].second);
            }
        }

        spdlog::warn("GET: {} - {}", res->status, ss.str());
    }

    return json::parse(res->body);
}

void web_client::_acquire() {
    if (_use_rate_limit) {
        _rate_limit.acquire();
    }
}
