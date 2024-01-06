#ifndef WEB_CLIENT_H
#define WEB_CLIENT_H

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <nlohmann/json.hpp>

#include "rate_limit.h"

class web_client_exception : public std::runtime_error {
    public:
    explicit web_client_exception(std::string_view method, int status, std::string_view msg);
};

/* JSON web client */
class web_client {
    httplib::Client _client;

    bool _use_rate_limit;
    rate_limit _rate_limit;

    public:
    using parameter = std::pair<std::string_view, std::string_view>;
    using json = nlohmann::json;

    explicit web_client(const std::string& url, size_t bucket_size = -1, rate_limit::duration refill_delay = rate_limit::duration(-1));

    [[nodiscard]] json get(const std::string& path, std::vector<parameter> params = {});

    private:
    /* Ensure rate limiting requirements */
    void _acquire();
};

#endif /* WEB_CLIENT_H */
