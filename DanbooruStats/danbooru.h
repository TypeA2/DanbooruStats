#ifndef DANBOORU_H
#define DANBOORU_H

#include "web_client.h"

class danbooru {
    std::string _username;
    std::string _api_key;

    web_client _client;

    rate_limit _rate_limit;

    public:
    explicit danbooru(std::string_view username, std::string_view api_key);

    [[nodiscard]] bool user_exists(uint64_t id);
    [[nodiscard]] bool user_exists(uint64_t id, std::string& user_name);

    private:
    [[nodiscard]] bool _check_login();

    /* Automatically insert API credentials */
    [[nodiscard]] web_client::json _get(const std::string& path, std::vector<web_client::parameter> params = {});

    [[nodiscard]] bool _user_exists(uint64_t id, std::string* user_name);
};

#endif /* DANBOORU_H */
