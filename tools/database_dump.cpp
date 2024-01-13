/*
 * Notes:
 * - Roundtrip works
 * - Negative integers always take up 10 bytes, whoops
 * - Writing is really really (1/100th the speed) slow
 **/

#include <iostream>
#include <fstream>
#include <format>
#include <filesystem>
#include <array>
#include <variant>
#include <vector>
#include <span>
#include <chrono>

#include <SQLiteCpp/SQLiteCpp.h>

template <> struct std::formatter<std::chrono::nanoseconds> {
	constexpr auto parse(format_parse_context& ctx) {
		return ctx.begin();
	}

	auto format(const std::chrono::nanoseconds& ns, format_context& ctx) const {
		if (ns < std::chrono::microseconds(1)) {
			return std::format_to(ctx.out(), "{} ns", ns.count());
		} else if (ns < std::chrono::milliseconds(1)) {
			return std::format_to(ctx.out(), "{:.3f} us", ns.count() / 1e3);
		} else if (ns < std::chrono::seconds(1)) {
			return std::format_to(ctx.out(), "{:.3f} ms", ns.count() / 1e6);
		}

		return std::format_to(ctx.out(), "{:.3f} s", ns.count() / 1e9);
	}
};

struct format_bytes {
	format_bytes(size_t count) : count { count } { }
	size_t count;
};

template <> struct std::formatter<format_bytes> {
	constexpr auto parse(format_parse_context& ctx) {
		return ctx.begin();
	}

	auto format(const format_bytes& bytes, format_context& ctx) const {
		if (bytes.count < 1024) {
			return std::format_to(ctx.out(), "{} bytes", bytes.count);
		} else if (bytes.count < size_t { 1024 } *1024) {
			return std::format_to(ctx.out(), "{:.3f} KiB", bytes.count / 1024.);
		} else if (bytes.count < size_t { 1024 } *1024 * 1024) {
			return std::format_to(ctx.out(), "{:.3f} MiB", bytes.count / 1024. / 1024.);
		} else if (bytes.count < size_t { 1024 } *1024 * 1024 * 1024) {
			return std::format_to(ctx.out(), "{:.3f} GiB", bytes.count / 1024. / 1024. / 1024.);
		}

		return std::format_to(ctx.out(), "{:.3f} TiB", bytes.count / 1024. / 1024. / 1024. / 1024.);
	}
};

using std::chrono::steady_clock;

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

[[nodiscard]] int parse_sqlite(const std::filesystem::path& in, std::ostream& out);
[[nodiscard]] int generate_sqlite(std::istream& in, const std::filesystem::path& out);

[[nodiscard]] std::array<char, 4> get_fourcc(const std::filesystem::path& path) {
	std::ifstream is { path, std::ios::binary };

	std::array<char, 4> res {};
	is.read(res.data(), res.size());
	return res;
}

[[nodiscard]] bool confirm_overwrite(std::string_view name) {
	std::print(std::cerr, "File \"{}\" already exists, overwrite? (y/n): ", name);
	std::string reply;
	std::cin >> reply;

	return (std::tolower(reply.front()) == 'y');
}

int main(int argc, char** argv) {
	if (argc != 1 && argc != 3) {
		std::println(std::cerr, "Usage: {} <input> <output>", argv[0]);
		return EXIT_FAILURE;
	}

	std::string input;
	std::string output;

	if (argc == 1) {
		std::print(std::cerr, "Input file: ");
		std::getline(std::cin, input);

		std::print(std::cerr, "Output file: ");
		std::getline(std::cin, output);
	} else {
		input = argv[1];
		output = argv[2];
	}

	try {
		if (input == std::string_view { "-" }) {
			/* stdin input */
			if (output == std::string_view { "-" }) {
				std::println(std::cerr, "Cannot write sqlite to stdout");
				return EXIT_FAILURE;
			}

			std::filesystem::path outpath = output;
			if (exists(outpath) && !confirm_overwrite(output)) {
				return EXIT_FAILURE;
			}

			return generate_sqlite(std::cin, outpath);
		}

		if (output == std::string_view { "-" }) {
			/* stdout output */
			if (argv[1] == std::string_view { "-" }) {
				std::println(std::cerr, "Cannot read sqlite from stdin");
				return EXIT_FAILURE;
			}

			std::filesystem::path inpath = input;
			if (!exists(inpath)) {
				std::println(std::cerr, "File \"{}\" does not exist", input);
				return EXIT_FAILURE;
			}

			return parse_sqlite(inpath, std::cout);
		}

		/* File in- and output */
		std::filesystem::path inpath = input;
		std::filesystem::path outpath = output;

		if (!exists(inpath)) {
			std::println(std::cerr, "File \"{}\" does not exist", input);
			return EXIT_FAILURE;
		}

		if (exists(outpath) && !confirm_overwrite(output)) {
			return EXIT_FAILURE;
		}

		auto fourcc = get_fourcc(inpath);
		if (fourcc == std::array { 'e', 'v', 'a', 'z' }) {
			/* Input file is our format */
			std::println(std::cerr, "Generating SQLite 3 database from {}", input);
			std::ifstream in { inpath, std::ios::binary };
			return generate_sqlite(in, outpath);
		} else {
			std::println(std::cerr, "Parsing SQLite 3 database {}", input);
			std::ofstream out { outpath, std::ios::binary };
			return parse_sqlite(inpath, out);
		}
	} catch (const std::exception& e) {
		std::println(std::cerr, "Exception: {}", e.what());
		return EXIT_FAILURE;
	}
}

struct varint {
	int64_t value;

	varint() = default;
	varint(int64_t v) : value { v } { }

	varint& operator=(int64_t v) { value = v; return *this; }
};

static std::ostream& operator<<(std::ostream& os, varint val) {
	/* Write 7 bits at a time, with the 8 bit indicating whether a byte follows or not */
	uint64_t value = val.value;

	/* Always write at least 1 byte */
	do {
		uint8_t byte = value & 0x7F;

		value >>= 7;

		/* Set upper bit if there's more bytes */
		if (value) {
			byte |= 0x80;
		}

		os.write(reinterpret_cast<char*>(&byte), 1);
	} while (value);

	return os;
}

static std::istream& operator>>(std::istream& is, varint& val) {
	uint8_t byte = 0;
	uint64_t value = 0;
	size_t offset = 0;
	do {
		is.read(reinterpret_cast<char*>(&byte), 1);

		value |= uint64_t(byte & 0x7F) << offset;

		offset += 7;

		/* Keep reading as long as upper bit is set */
	} while (byte & 0x80);

	val.value = value;

	return is;
}

struct sqlite_value {
	enum class type : uint8_t {
		null    = 0,
		integer = 1,
		real    = 2,
		text    = 3,
		blob    = 4,
	};

	using value_type = std::variant<std::monostate, int64_t, double, std::string, std::vector<std::byte>>;

	type type = type::null;
	value_type value = std::monostate {};

	static sqlite_value from_column(const SQLite::Column& col) {
		sqlite_value val;

		int type = col.getType();
		if (type == SQLite::INTEGER) {
			val.type = type::integer;
		} else if (type == SQLite::FLOAT) {
			val.type = type::real;
		} else if (type == SQLite::TEXT) {
			val.type = type::text;
		} else if (type == SQLite::BLOB) {
			val.type = type::blob;
		}

		switch (val.type) {
			case type::integer: val.value = col.getInt64(); break;
			case type::real: val.value = col.getDouble(); break;
			case type::text: val.value = col.getString(); break;
			case type::blob: {
				std::vector<std::byte> blob(col.getBytes());
				std::copy_n(static_cast<const std::byte*>(col.getBlob()), blob.size(), blob.begin());
				val.value = std::move(blob);
				break;
			}
		}

		return val;
	}
};

static std::ostream& operator<<(std::ostream& os, const sqlite_value& val) {
	os.write(reinterpret_cast<const char*>(&val.type), 1);

	switch (val.type) {
		case sqlite_value::type::null: break;
		case sqlite_value::type::integer:
			os << varint(std::get<int64_t>(val.value));
			break;

		case sqlite_value::type::real: {
			double value = std::get<double>(val.value);
			os.write(reinterpret_cast<char*>(&value), 8);
			break;
		}
		case sqlite_value::type::text: {
			const std::string& text = std::get<std::string>(val.value);
			os.write(text.c_str(), text.size() + 1);
			break;
		}

		case sqlite_value::type::blob: {
			const std::vector<std::byte>& blob = std::get<std::vector<std::byte>>(val.value);
			os << varint(blob.size());
			os.write(reinterpret_cast<const char*>(blob.data()), blob.size());
			break;
		}
	}

	return os;
}

static std::istream& operator>>(std::istream& is, sqlite_value& val) {
	is.read(reinterpret_cast<char*>(&val.type), 1);

	switch (val.type) {
		case sqlite_value::type::null: break;
		case sqlite_value::type::integer: {
			varint value;
			is >> value;
			val.value = value.value;
			break;
		}

		case sqlite_value::type::real: {
			double value;
			is.read(reinterpret_cast<char*>(&value), 8);
			val.value = value;
			break;
		}
		case sqlite_value::type::text: {
			std::string str;
			std::getline(is, str, '\0');
			val.value = std::move(str);
			break;
		}

		case sqlite_value::type::blob: {
			varint size;
			is >> size;
			std::vector<std::byte> blob(size.value);
			is.read(reinterpret_cast<char*>(blob.data()), blob.size());
			val.value = std::move(blob);
			break;
		}
	}

	return is;
}

struct column {
	std::string name;
	std::string type;
	bool not_null;
	bool primary_key;
	sqlite_value value;
};

static std::ostream& operator<<(std::ostream& os, const column& col) {
	os.write(col.name.c_str(), col.name.size() + 1);
	os.write(col.type.c_str(), col.type.size() + 1);
	uint8_t flags = 0;
	flags |= (col.not_null ? 1 : 0);
	flags |= (col.primary_key ? 0b10 : 0b00);
	os.write(reinterpret_cast<char*>(&flags), 1);
	os << col.value;
	return os;
}

static std::istream& operator>>(std::istream& is, column& col) {
	std::getline(is, col.name, '\0');
	std::getline(is, col.type, '\0');
	int flags = is.get();
	col.not_null = (flags & 1) ? true : false;
	col.primary_key = (flags & 0b10) ? true : false;
	is >> col.value;
	return is;
}

int parse_sqlite(const std::filesystem::path& in, std::ostream& out) {
	SQLite::Database db { in.string(), SQLite::OPEN_READONLY };

	std::string table;
	std::string table_sql;
	{
		SQLite::Statement query(db, "SELECT COUNT(*), name, sql FROM sqlite_master WHERE type = 'table'");
		query.executeStep();

		if (int tables = query.getColumn(0).getInt(); tables != 1) {
			if (tables == 0) {
				std::println (std::cerr, "No tables present in database");
				return EXIT_FAILURE;
			} else {
				std::println(std::cerr, "Too many tables present in database. Expected 1, got {}", tables);
				return EXIT_FAILURE;
			}
		}

		table = query.getColumn(1).getString();
		table_sql = query.getColumn(2).getString();
	}

	std::println(std::cerr, "Found table {}", table);

	std::vector<column> columns;
	{
		SQLite::Statement query(db, std::format("PRAGMA table_info({})", table));
		while (query.executeStep()) {
			column col {
				.name        = query.getColumn(1).getString(),
				.type        = query.getColumn(2).getString(),
				.not_null    = query.getColumn(3).getInt() ? true : false,
				.primary_key = query.getColumn(5).getInt() ? true : false,
				.value       = sqlite_value::from_column(query.getColumn(4)),
			};

			columns.emplace_back(std::move(col));
		}
	}

	std::println(std::cerr, "Found {} columns:", columns.size());
	for (const column& col : columns) {
		std::println(std::cerr,"  {} {}{}{}",
			col.name, col.type,
			col.not_null ? " NOT NULL" : "", col.primary_key ? " PRIMARY KEY" : "");
	}

	auto begin_offset = out.tellp();
	auto begin = steady_clock::now();

	out.write("evaz", 4); /* fourcc */
	out.write(table.c_str(), table.size() + 1);
	out.write(table_sql.c_str(), table_sql.size() + 1);
	out << varint(columns.size());

	size_t rows = 0;

	{
		SQLite::Statement query(db, std::format("SELECT * FROM {}", table));
		while (query.executeStep()) {
			for (int i = 0; i < columns.size(); ++i) {
				sqlite_value val = sqlite_value::from_column(query.getColumn(i));
				out << val;
			}
			++rows;
		}
	}

	auto elapsed = steady_clock::now() - begin;
	size_t written = out.tellp() - begin_offset;

	size_t write_speed = size_t(written / (elapsed.count() / 1e9));

	std::println(std::cerr, "Wrote {} columns, {} rows in {} ({}, {}/s)",
		columns.size(), rows, elapsed, format_bytes { written }, format_bytes { write_speed });

	return EXIT_SUCCESS;
}

int generate_sqlite(std::istream& in, const std::filesystem::path& out) {
	std::filesystem::remove(out);

	SQLite::Database db { out.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE };
	auto begin_offset = in.tellg();
	
	auto preamble_begin = steady_clock::now();
	std::array<char, 4> evaz;
	in.read(evaz.data(), 4);
	if (evaz != std::array { 'e', 'v', 'a', 'z' }) {
		std::println(std::cerr, "Unexpected fourcc: {}", std::string_view { evaz });
		return EXIT_FAILURE;
	}

	std::string table;
	std::getline(in, table, '\0');

	std::string table_sql;
	std::getline(in, table_sql, '\0');

	varint col_count;
	in >> col_count;

	auto preamble_elapsed = steady_clock::now() - preamble_begin;

	std::println(std::cerr, "Writing table {}", table);

	std::println(std::cerr, "Found {} columns", col_count.value);

	db.exec(table_sql);

	std::stringstream insert_stmt;
	std::print(insert_stmt, "INSERT INTO `{}` VALUES (", table);
	for (int64_t i = 1; i < col_count.value; ++i) {
		std::print(insert_stmt, "?, ");
	}
	std::print(insert_stmt, "?);");

	SQLite::Statement insert { db, insert_stmt.str()};

	auto begin = steady_clock::now();

	size_t rows = 0;

	std::streamoff last_pos = in.tellg();

	while (in) {
		insert.reset();
		insert.clearBindings();

		bool done = false;

		for (int i = 0; i < col_count.value; ++i) {
			sqlite_value value;
			if (!(in >> value)) {
				done = true;
				break;
			}

			last_pos = in.tellg();

			std::visit(overloaded {
				[] (std::monostate) { },
				[&](const std::vector<std::byte>& blob) { insert.bind(i + 1, blob.data(), int(blob.size())); },
				[&](const auto& val) { insert.bind(i + 1, val); }
			}, value.value);
		}

		if (done) {
			break;
		}

		insert.exec();

		++rows;
	}

	auto elapsed = preamble_elapsed + (steady_clock::now() - begin);
	size_t written = last_pos - begin_offset;
	size_t read_speed = size_t(written / (elapsed.count() / 1e9));

	std::println(std::cerr, "Read {} columns, {} rows in {} ({}, {}/s)",
		col_count.value, rows, elapsed, format_bytes { written }, format_bytes { read_speed });

	return EXIT_SUCCESS;
}