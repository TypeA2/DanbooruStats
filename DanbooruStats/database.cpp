#include "database.h"

#include <format>

database::database(const std::string& path)
    : _db{ path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE } {
    _create_tables();
}

database::~database() {
    
}

user_status database::status(int64_t id) {
    auto& query = _queries.at(query::user_status);
    query.reset();
    query.bind(1, id);
    
    if (!query.executeStep()) {
        throw std::runtime_error{ std::format("User ID {} not found", id) };
    }

    return static_cast<user_status>(query.getColumn(0).getInt());
}

void database::_create_tables() {
    _db.exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, status INTEGER)");

    _queries.emplace(query::user_status, SQLite::Statement{ _db, "SELECT status FROM users WHERE id = ?" });
}
