#ifndef DATABASE_H
#define DATABASE_H

#include <SQLiteCpp/SQLiteCpp.h>

#include <unordered_map>

enum class user_status {
    pending,
    fetching_posts,
    fetching_tags,
    done,
};

class database {
    SQLite::Database _db;

    public:
    explicit database(const std::string& path);

    ~database();

    user_status status(int64_t id);

    private:
    void _create_tables();

    /* Prepared statements for query reuse */
    enum class query {
        user_status
    };
    
    std::unordered_map<query, SQLite::Statement> _queries;
};

#endif /* DATABASE_H */