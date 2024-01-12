#include <iostream>
#include <fstream>
#include <string>
#include <format>
#include <ranges>
#include <algorithm>
#include <chrono>

#include <ctre.hpp>

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
            return std::format_to(ctx.out(), "{}", bytes.count);
        } else if (bytes.count < size_t { 1024 } * 1024) {
            return std::format_to(ctx.out(), "{:.3f} KiB", bytes.count / 1024.);
        } else if (bytes.count < size_t { 1024 } * 1024 * 1024) {
            return std::format_to(ctx.out(), "{:.3f} MiB", bytes.count / 1024. / 1024.);
        } else if (bytes.count < size_t { 1024 } * 1024 * 1024 * 1024) {
            return std::format_to(ctx.out(), "{:.3f} GiB", bytes.count / 1024. / 1024. / 1024.);
        }

        return std::format_to(ctx.out(), "{:.3f} TiB", bytes.count / 1024. / 1024. / 1024. / 1024.);
    }
};

using std::chrono::steady_clock;

[[nodiscard]] int translate_sql(std::istream& in, std::ostream& out);

int main(int argc, char** argv) {
    switch (argc) {
        case 1:
            /* No arguments: stdin -> stdout */
            return translate_sql(std::cin, std::cout);

        case 2: {
            /* 1 argument: input file -> stdout */
            std::ifstream in { argv[1] };
            if (!in) {
                std::print(std::cerr, "Failed to open '{}'\n", argv[1]);
                return EXIT_FAILURE;
            }

            return translate_sql(in, std::cout);
        }

        case 3: {
            /* 2 arguments: input file -> output file */
            std::ifstream in { argv[1] };
            if (!in) {
                std::print(std::cerr, "Failed to open '{}'\n", argv[1]);
                return EXIT_FAILURE;
            }

            std::ofstream out { argv[2] };
            if (!out) {
                std::print(std::cerr, "Failed to open '{}'\n", argv[2]);
                return EXIT_FAILURE;
            }

            return translate_sql(in, out);
        }
        default:
            std::print(std::cerr, "Usage: {} [infile] [outfile]\n", argv[0]);
            return EXIT_FAILURE;
    }
}

int translate_sql(std::istream& in, std::ostream& out) {
    auto begin = steady_clock::now();

    size_t lines = 0;
    size_t bytes_read = 0;
    size_t bytes_written = 0;

    std::chrono::nanoseconds sec1 { 0 };
    std::chrono::nanoseconds sec2 { 0 };
    std::chrono::nanoseconds sec3 { 0 };
    std::chrono::nanoseconds sec4 { 0 };

    /* Whether we are in the preamble, postamble, or neither */
    bool preamble = true;
    bool postamble = false;

    for (std::string line; std::getline(in, line);) {
        ++lines;
        bytes_read += line.size() + 1; /* +1 for newline */

        auto a = steady_clock::now();

        if (preamble || postamble) {
            if (line.starts_with("LOCK")) {
                /* Comment LOCK */
                out << "-- ";
                bytes_written += 3;

                preamble = false;

            } else if (line.starts_with(") ENGINE")) {
                /* Engine and other specifiers are ignored */
                out << "); -- ";
                bytes_written += 6;
                line = line.substr(2);
            } else if (auto match = ctre::starts_with<R"(\s+KEY)">(line); match) {
                /* Comment out KEY statements */
                out << "-- ";
                bytes_written += 3;
            } else if (auto match = ctre::match<R"(\s+PRIMARY KEY.+(,))">(line); match) {
                /* Remove trailing comma from final PRIMARY KEY statement*/
                //out << match.get<1>();
                //bytes_written += match.get<1>().size();
                *match.get<1>().begin() = ' ';
            }
        } else if (line.starts_with("UNLOCK")) {
            /* Comment UNLOCK */
            out << "-- ";
            bytes_written += 3;

            postamble = true;
        }

        auto b = steady_clock::now();

        if (preamble) {
            /* Remove unsigned specifier */
            size_t remaining = line.length();
            auto it = line.begin();
            for (std::string_view sv = line; sv.size() >= 8; ++it, sv = { it, line.end() }) {
                if (sv.starts_with("unsigned") || sv.starts_with("UNSIGNED")) {
                    std::fill_n(it, 8, ' ');
                    it += 7;
                }
            }
        }

        auto c = steady_clock::now();

        if (!preamble && !postamble) {
            /* Fix quotes */
            bool escape_next = false;
            for (auto it = line.begin(); it != line.end(); ++it) {
                if (escape_next) {
                    /* If the escaped character is a single quote, fix the sequence */
                    if (*it == '\'') {
                        *(it - 1) = '\'';
                    }

                    escape_next = false;
                } else if (*it == '\\') {
                    /* Next character is escaped */
                    escape_next = true;
                }
            }
        }

        auto d = steady_clock::now();

        out << line << '\n';
        bytes_written += line.size() + 1;

        auto e = steady_clock::now();

        sec1 += (b - a);
        sec2 += (c - b);
        sec3 += (d - c);
        sec4 += (e - d);
    }

    auto end = steady_clock::now();
    auto elapsed = end - begin;

    size_t read_per_second = size_t(bytes_read / (elapsed.count() / 1e9));
    size_t write_per_second = size_t(bytes_written / (elapsed.count() / 1e9));

    std::print(std::cerr, "Processed {} lines in {}:\n", lines, elapsed);
    std::print(std::cerr, "  {} {} {} {}\n", sec1, sec2, sec3, sec4);
    std::print(std::cerr, "Read {} ({}/s), wrote {} ({}/s)\n",
        format_bytes { bytes_read }, format_bytes { read_per_second },
        format_bytes { bytes_written }, format_bytes { write_per_second });


    return EXIT_SUCCESS;
}
