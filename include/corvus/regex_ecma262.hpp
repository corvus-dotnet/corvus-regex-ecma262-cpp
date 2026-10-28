// corvus/regex_ecma262.hpp: ECMA-262 (JavaScript) regular expressions for C++20, with the grammar and the answers of
// RegExp, over UTF-8 and UTF-16 text. A header-only library over the corvus-regex-ecma262 C library
// (corvus_regex_ecma262.h), which it links as any C consumer does.
//
//     #include <corvus/regex_ecma262.hpp>
//
//     corvus::ecma262::regex re(R"((?<year>\d{4})-(?<month>\d{2}))", "gu");
//     for (const corvus::ecma262::match& m : re.find_all("2026-10, 2027-01")) { ... }
//
// The conventions:
//
// - A regex is a compiled pattern, read as new RegExp(pattern, flags) reads it. It is immutable, so any number of
//   threads may use one at once, and copying one shares the compiled pattern rather than compiling it again.
// - Text is any std::basic_string_view of char or char8_t (UTF-8, checked: text that is not UTF-8 throws) or of
//   char16_t (UTF-16, not checked, lone surrogates and all), and so a std::basic_string or a pointer to a
//   NUL-terminated string of those. On Windows, wchar_t text is UTF-16. Nothing is transcoded: positions are in the
//   units of the text, bytes of UTF-8 or UTF-16 code units.
// - A match is the one exec finds. Without the u or v flag a pattern is matched by UTF-16 code unit, so over UTF-8 a
//   match can start or end between the halves of a character beyond the Basic Multilingual Plane; such a position is
//   the offset of the character's third byte.
// - Failures throw corvus::ecma262::regex_error, whose code() says why (std::error_code, in ecma262::category()). A
//   pattern that is not valid is errc::invalid_pattern; a match that reaches its limit of time or of backtracking
//   memory is errc::match_limit.
// - A regex has a match timeout, as a Regex of .NET has: the constructor that takes a std::chrono::milliseconds, or
//   regex::set_default_match_timeout for every regex made after it (none unless set). Every member that matches gives
//   each of its matches the timeout, and the overloads that take a std::chrono::milliseconds give one match a limit
//   of their own. Without a timeout a match has no limit of time or of memory, so give a pattern or a text from
//   outside one.
// - A pattern is read in the grammar of an edition (edition::current, today's, or edition::es2020, which JSON Schema
//   2020-12 names) and under a version of Unicode (unicode_version::v8, the version of the V8 the answers are held to,
//   17 today; v13, v17, v18; or latest, 18 today), each independent of the other and settled when it is compiled, so
//   that they cost a match nothing: regex::options carries them with a match timeout. regex::try_compile is the form
//   that does not throw, and takes the options too, so a validator needs no default timeout.
// - A global search (find_all, captures_all) is a range that keeps the regex alive but not the text, as a
//   std::string_view does not: the text must outlive it.
//
// Licensed under the Apache License, Version 2.0.
#ifndef CORVUS_REGEX_ECMA262_HPP
#define CORVUS_REGEX_ECMA262_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "corvus_regex_ecma262.h"

// The version of this library; it is the C library's, whose version the header names (CRE_VERSION_*).
#define CORVUS_REGEX_ECMA262_CPP_VERSION_MAJOR 0
#define CORVUS_REGEX_ECMA262_CPP_VERSION_MINOR 2
#define CORVUS_REGEX_ECMA262_CPP_VERSION_PATCH 3

namespace corvus::ecma262 {

// Why a call failed: the C library's statuses other than CRE_OK.
enum class errc : std::uint32_t {
    // A value out of range, such as a start beyond the text or inside a character.
    invalid_argument = CRE_INVALID_ARGUMENT,
    // Text that is not UTF-8 (regex_error::offset() is the offset of the first invalid byte).
    invalid_utf8 = CRE_INVALID_UTF8,
    // The pattern is not valid in the grammar of its flags (new RegExp would throw a SyntaxError).
    invalid_pattern = CRE_INVALID_PATTERN,
    // A letter that is not one of dgimsuvy, a flag given twice, or u with v.
    invalid_flags = CRE_INVALID_FLAGS,
    // The pattern is over a limit of size (its length, the depth its groups nest to, or its size as it is read).
    pattern_too_large = CRE_PATTERN_TOO_LARGE,
    // A match reached its limit of time or of backtracking memory, so there is no answer.
    match_limit = CRE_MATCH_LIMIT,
    // A bug in the library: it was caught, and the library remains usable.
    panic = CRE_PANIC,
};

namespace detail {

class category_impl final : public std::error_category {
  public:
    const char* name() const noexcept override { return "corvus::ecma262"; }
    std::string message(int code) const override {
        cre_str name = cre_status_name(static_cast<cre_status>(code));
        return std::string(name.ptr, name.len);
    }
};

}  // namespace detail

// The error category of errc.
inline const std::error_category& category() noexcept {
    static const detail::category_impl instance;
    return instance;
}

inline std::error_code make_error_code(errc e) noexcept { return {static_cast<int>(e), category()}; }

// What a call that failed throws: the reason (code(), an errc), the C library's message (what()) and, for text that
// is not UTF-8, the offset of the first invalid byte (offset()).
class regex_error : public std::system_error {
  public:
    regex_error(errc code, const std::string& message, std::size_t offset = 0)
        : std::system_error(make_error_code(code), message), offset_(offset) {}

    // The reason, as an errc.
    errc reason() const noexcept { return static_cast<errc>(code().value()); }

    // The offset of the first byte that is not UTF-8, for errc::invalid_utf8; otherwise 0.
    std::size_t offset() const noexcept { return offset_; }

  private:
    std::size_t offset_;
};

// The edition of ECMA-262 whose grammar a pattern is read with: today's, the default, or ECMAScript 2020's. The 2020
// grammar refuses what later editions added: the d and v flags (errc::invalid_flags), modifier groups, a name given to
// more than one group, and property names that are not in its edition's tables (each errc::invalid_pattern).
enum class edition : std::uint32_t {
    current = CRE_EDITION_CURRENT,
    es2020 = CRE_EDITION_ES2020,
};

// The version of Unicode whose tables a pattern is read with (its properties and its case folding), independent of
// the edition: v8, the version of the V8 the answers are held to (17 today); 13, 17 or 18; or latest, the latest the C
// library carries (18 today).
enum class unicode_version : std::uint32_t {
    v8 = CRE_UNICODE_DEFAULT,
    v13 = CRE_UNICODE_13,
    v17 = CRE_UNICODE_17,
    v18 = CRE_UNICODE_18,
    latest = CRE_UNICODE_LATEST,
};

// The versions of Unicode a pattern can be read with; the one unicode_version::v8 stands for; and the one
// unicode_version::latest stands for.
inline constexpr std::array<int, 3> unicode_versions{13, 17, 18};
inline constexpr int default_unicode_version = 17;
inline constexpr int latest_unicode_version = 18;

// The position of no match and of a group that did not take part.
inline constexpr std::size_t npos = CRE_UNSET;

// Where a match or a group is in its text: start to end, in the text's units (bytes of UTF-8, or UTF-16 code
// units). A group that did not take part is npos to npos, and is false.
struct match {
    std::size_t start = npos;
    std::size_t end = npos;

    // Whether the group took part in the match.
    constexpr explicit operator bool() const noexcept { return start != npos; }

    // The length in units (0 for a group that did not take part).
    constexpr std::size_t length() const noexcept { return start == npos ? 0 : end - start; }

    // The part of the text, which must be the text the match was found in.
    template <class C>
    constexpr std::basic_string_view<C> in(std::basic_string_view<C> text) const noexcept {
        return start == npos ? std::basic_string_view<C>() : text.substr(start, end - start);
    }

    friend constexpr bool operator==(const match&, const match&) = default;
};

static_assert(sizeof(match) == sizeof(cre_match) && alignof(match) == alignof(cre_match),
              "match is laid out as cre_match");

// The limit of split that keeps every piece, as split(separator) with no limit.
inline constexpr std::uint32_t no_limit = CRE_SPLIT_ALL;

namespace detail {

// The code units the library reads: char and char8_t are UTF-8, char16_t (and wchar_t where it is 16 bits) UTF-16.
template <class C>
inline constexpr bool is_utf8_unit = std::is_same_v<C, char> || std::is_same_v<C, char8_t>;
template <class C>
inline constexpr bool is_utf16_unit =
    std::is_same_v<C, char16_t> || (std::is_same_v<C, wchar_t> && sizeof(wchar_t) == 2);

template <class C>
concept code_unit = is_utf8_unit<C> || is_utf16_unit<C>;

// The code unit of a text argument: a string view, a string, a pointer to a NUL-terminated string, or an array.
template <class T>
struct unit_of_impl {};
template <class C, class Traits>
struct unit_of_impl<std::basic_string_view<C, Traits>> {
    using type = C;
};
template <class C, class Traits, class Alloc>
struct unit_of_impl<std::basic_string<C, Traits, Alloc>> {
    using type = C;
};
template <class C>
struct unit_of_impl<C*> {
    using type = std::remove_const_t<C>;
};
template <class C, std::size_t N>
struct unit_of_impl<C[N]> {
    using type = std::remove_const_t<C>;
};

template <class T>
using unit_of = typename unit_of_impl<std::remove_cvref_t<T>>::type;

template <class T>
concept text = requires { typename unit_of<T>; } && code_unit<unit_of<T>> &&
               std::is_convertible_v<const T&, std::basic_string_view<unit_of<T>>>;

template <class T>
std::basic_string_view<unit_of<T>> view(const T& t) {
    return std::basic_string_view<unit_of<T>>(t);
}

inline const char* bytes(std::string_view s) noexcept { return s.data(); }
inline const char* bytes(std::u8string_view s) noexcept { return reinterpret_cast<const char*>(s.data()); }
template <class C>
const std::uint16_t* units(std::basic_string_view<C> s) noexcept {
    return reinterpret_cast<const std::uint16_t*>(s.data());
}

[[noreturn]] inline void raise(cre_status status) {
    cre_str m = cre_last_error_message();
    throw regex_error(static_cast<errc>(status), std::string(m.ptr, m.len), cre_last_error_offset());
}

inline void check(cre_status status) {
    if (status != CRE_OK) raise(status);
}

struct regex_deleter {
    void operator()(cre_regex* r) const noexcept { cre_regex_free(r); }
};
struct captures_deleter {
    void operator()(cre_captures* c) const noexcept { cre_captures_free(c); }
};
struct matches_deleter {
    void operator()(cre_matches* m) const noexcept { cre_matches_free(m); }
};

// UTF-16 to UTF-8, a lone surrogate as U+FFFD (as a pattern given as UTF-16 is read).
template <class C>
std::string utf8_of(std::basic_string_view<C> s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t c = static_cast<std::uint16_t>(s[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && static_cast<std::uint16_t>(s[i + 1]) >= 0xDC00 &&
            static_cast<std::uint16_t>(s[i + 1]) <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<std::uint16_t>(s[++i]) - 0xDC00);
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

inline std::uint64_t millis(std::chrono::milliseconds limit) {
    return limit.count() <= 0 ? 1 : static_cast<std::uint64_t>(limit.count());
}

// The milliseconds of a match timeout, 0 (none) for zero or less.
inline std::uint64_t timeout_millis(std::chrono::milliseconds timeout) {
    return timeout.count() <= 0 ? 0 : static_cast<std::uint64_t>(timeout.count());
}

// A match timeout from the C library's milliseconds, nullopt for none.
inline std::optional<std::chrono::milliseconds> timeout_of(std::uint64_t ms) {
    if (ms == 0) return std::nullopt;
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(ms));
}

}  // namespace detail

class regex;

// What a match captured: each group by number (0 is the whole match) and by name, with the text it was found in.
// It is what regex::captures returns, and what a captures_all range gives for each match (reused from one match to
// the next, so copy what must outlive the step).
template <class C>
class basic_captures {
  public:
    // The number of groups, the whole match among them.
    std::size_t size() const noexcept { return cre_captures_len(handle_.get()); }

    // Where group i is (false when it did not take part, or there is no such group).
    match operator[](std::size_t i) const noexcept {
        cre_match m{npos, npos};
        cre_captures_get(handle_.get(), i, &m);
        return {m.start, m.end};
    }

    // Where the group of the name is (of the groups that share it, the one that took part).
    match operator[](std::string_view name) const noexcept {
        cre_match m{npos, npos};
        cre_captures_name(handle_.get(), name.data(), name.size(), &m);
        return {m.start, m.end};
    }

    // The text of group i, or nothing when it did not take part.
    std::optional<std::basic_string_view<C>> str(std::size_t i) const noexcept { return text_of((*this)[i]); }

    // The text of the group of the name, or nothing when no group of that name took part.
    std::optional<std::basic_string_view<C>> str(std::string_view name) const noexcept {
        return text_of((*this)[name]);
    }

    // The whole match.
    match whole() const noexcept { return (*this)[0]; }

    // The text the match was found in.
    std::basic_string_view<C> text() const noexcept { return text_; }

  private:
    friend class regex;
    template <class>
    friend class basic_captures_range;

    basic_captures(std::unique_ptr<cre_captures, detail::captures_deleter> handle, std::basic_string_view<C> text)
        : handle_(std::move(handle)), text_(text) {}

    std::optional<std::basic_string_view<C>> text_of(match m) const noexcept {
        if (!m) return std::nullopt;
        return m.in(text_);
    }

    std::unique_ptr<cre_captures, detail::captures_deleter> handle_;
    std::basic_string_view<C> text_;
};

using captures = basic_captures<char>;
using u8captures = basic_captures<char8_t>;
using u16captures = basic_captures<char16_t>;

// A global search of one text, as matchAll makes one, as a range of match: after an empty match the search goes on
// one character later (one code point with u or v, one UTF-16 code unit without), and with the y flag each match
// must start where the one before it ended. It reads the matches from the library in batches. It is an input
// range: it is read once, and begin() may be called once (again after reset()).
template <class C>
class basic_match_range : public std::ranges::view_interface<basic_match_range<C>> {
    static constexpr std::size_t batch = 256;

    struct state {
        std::shared_ptr<const cre_regex> regex;
        std::unique_ptr<cre_matches, detail::matches_deleter> handle;
        std::array<cre_match, batch> buffer;
        std::size_t count = 0;
        std::size_t at = 0;
        bool done = false;

        void fill() {
            at = 0;
            count = 0;
            if (done) return;
            detail::check(cre_matches_next_batch(handle.get(), buffer.data(), batch, &count));
            if (count < batch) done = true;
        }
    };

  public:
    class iterator {
      public:
        using value_type = match;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        iterator() = default;

        match operator*() const noexcept {
            const cre_match& m = s_->buffer[s_->at];
            return {m.start, m.end};
        }

        iterator& operator++() {
            if (++s_->at == s_->count) s_->fill();
            return *this;
        }

        void operator++(int) { ++*this; }

        friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept {
            return it.s_->at == it.s_->count;
        }

      private:
        friend class basic_match_range;
        explicit iterator(state* s) : s_(s) {}
        state* s_ = nullptr;
    };

    basic_match_range(basic_match_range&&) noexcept = default;
    basic_match_range& operator=(basic_match_range&&) noexcept = default;

    // The first match. It reads the first batch.
    iterator begin() {
        s_->fill();
        return iterator(s_.get());
    }

    std::default_sentinel_t end() const noexcept { return {}; }

    // Starts the search again at the start of its text, which is not checked again (as setting lastIndex to 0), so
    // that the same text can be searched again without a new search: begin() may then be called once more.
    void reset() noexcept {
        cre_matches_reset(s_->handle.get());
        s_->count = 0;
        s_->at = 0;
        s_->done = false;
    }

    // Every match, into a vector.
    std::vector<match> to_vector() {
        std::vector<match> out;
        for (match m : *this) out.push_back(m);
        return out;
    }

  private:
    friend class regex;

    basic_match_range(std::shared_ptr<const cre_regex> re, cre_matches* handle) : s_(std::make_unique<state>()) {
        s_->regex = std::move(re);
        s_->handle.reset(handle);
    }

    std::unique_ptr<state> s_;
};

using match_range = basic_match_range<char>;
using u8match_range = basic_match_range<char8_t>;
using u16match_range = basic_match_range<char16_t>;

// A global search as basic_match_range, keeping each match's groups: a range of basic_captures, one object reused
// from match to match (so copy what must outlive the step). An input range, read once.
template <class C>
class basic_captures_range : public std::ranges::view_interface<basic_captures_range<C>> {
    struct state {
        std::shared_ptr<const cre_regex> regex;
        std::unique_ptr<cre_matches, detail::matches_deleter> handle;
        basic_captures<C> current;
        bool found = false;

        void next() { detail::check(cre_matches_next_captures(handle.get(), current.handle_.get(), &found)); }
    };

  public:
    class iterator {
      public:
        using value_type = basic_captures<C>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        iterator() = default;

        const basic_captures<C>& operator*() const noexcept { return s_->current; }
        const basic_captures<C>* operator->() const noexcept { return &s_->current; }

        iterator& operator++() {
            s_->next();
            return *this;
        }

        void operator++(int) { ++*this; }

        friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept { return !it.s_->found; }

      private:
        friend class basic_captures_range;
        explicit iterator(state* s) : s_(s) {}
        state* s_ = nullptr;
    };

    basic_captures_range(basic_captures_range&&) noexcept = default;
    basic_captures_range& operator=(basic_captures_range&&) noexcept = default;

    iterator begin() {
        s_->next();
        return iterator(s_.get());
    }

    std::default_sentinel_t end() const noexcept { return {}; }

  private:
    friend class regex;

    basic_captures_range(std::shared_ptr<const cre_regex> re, cre_matches* handle, basic_captures<C> captures)
        : s_(std::unique_ptr<state>(new state{std::move(re), {}, std::move(captures), false})) {
        s_->handle.reset(handle);
    }

    std::unique_ptr<state> s_;
};

using captures_range = basic_captures_range<char>;
using u8captures_range = basic_captures_range<char8_t>;
using u16captures_range = basic_captures_range<char16_t>;

// A compiled ECMA-262 regular expression.
class regex {
  public:
    // How a pattern is compiled besides its flags: its match timeout (nullopt for the default timeout, zero for
    // none), the edition whose grammar it is read with, and the version of Unicode whose tables it is read with.
    struct options {
        std::optional<std::chrono::milliseconds> match_timeout;
        ecma262::edition edition = ecma262::edition::current;
        ecma262::unicode_version unicode_version = ecma262::unicode_version::v8;
    };

    // Compiles a pattern with the flags new RegExp(pattern, flags) takes, any of dgimsuvy. Throws regex_error:
    // errc::invalid_pattern, invalid_flags or pattern_too_large. A pattern given as UTF-16 is read as UTF-8, a lone
    // surrogate as U+FFFD.
    template <detail::text P>
    explicit regex(const P& pattern, std::string_view flags = {}) {
        compile(detail::view(pattern), flags, options{});
    }

    // As above, with a match timeout: the time each match may take before it is abandoned with errc::match_limit
    // (zero for none). A timeout also caps the backtracking memory of a match.
    template <detail::text P>
    regex(const P& pattern, std::string_view flags, std::chrono::milliseconds match_timeout) {
        compile(detail::view(pattern), flags, options{match_timeout});
    }

    // As above, with the options: under edition::es2020 the d and v flags are errc::invalid_flags.
    template <detail::text P>
    regex(const P& pattern, std::string_view flags, const options& opts) {
        compile(detail::view(pattern), flags, opts);
    }

    // The constructor that does not throw for the pattern: the regex, or nullopt with ec set to why (errc::
    // invalid_pattern, invalid_flags or pattern_too_large). It takes the options, so a check of a pattern from outside
    // gives it a timeout of its own without the default.
    template <detail::text P>
    static std::optional<regex> try_compile(const P& pattern, std::string_view flags, const options& opts,
                                            std::error_code& ec) {
        cre_regex* re = nullptr;
        cre_status status = new_regex(detail::view(pattern), flags, opts, &re);
        if (status != CRE_OK) {
            ec = make_error_code(static_cast<errc>(status));
            return std::nullopt;
        }
        ec.clear();
        return regex(re);
    }

    // Sets the match timeout of every regex made from now on without one of its own, or nullopt (or zero) for none,
    // the default. It is the process-wide default of .NET's REGEX_DEFAULT_MATCH_TIMEOUT; a program that matches
    // patterns or texts from outside should set it when it starts. A regex already made keeps its own.
    static void set_default_match_timeout(std::optional<std::chrono::milliseconds> timeout) noexcept {
        cre_set_default_match_timeout(timeout ? detail::timeout_millis(*timeout) : 0);
    }

    // The match timeout of a regex made now without one of its own, or nullopt for none.
    static std::optional<std::chrono::milliseconds> default_match_timeout() noexcept {
        return detail::timeout_of(cre_default_match_timeout());
    }

    // The time each match may take before it is abandoned with errc::match_limit, or nullopt for none.
    std::optional<std::chrono::milliseconds> match_timeout() const noexcept {
        return detail::timeout_of(cre_regex_match_timeout(re_.get()));
    }

    // Whether a pattern is valid with the flags, without compiling it. A pattern over a limit of size is not valid
    // here.
    template <detail::text P>
    static bool is_valid(const P& pattern, std::string_view flags = {}) {
        return is_valid(pattern, flags, ecma262::edition::current, ecma262::unicode_version::v8);
    }

    // As above, in the grammar of an edition and under a version of Unicode.
    template <detail::text P>
    static bool is_valid(const P& pattern, std::string_view flags, ecma262::edition ed, ecma262::unicode_version uv) {
        std::string p = pattern_bytes(detail::view(pattern));
        bool valid = false;
        detail::check(cre_regex_is_valid_with_options(p.data(), p.size(), flags.data(), flags.size(),
                                                      static_cast<std::uint32_t>(ed), static_cast<std::uint32_t>(uv),
                                                      &valid));
        return valid;
    }

    // The edition whose grammar the pattern was read with.
    ecma262::edition edition() const noexcept { return static_cast<ecma262::edition>(cre_regex_edition(re_.get())); }

    // The version of Unicode whose tables the pattern was read with, resolved: 13, 17 or 18.
    int unicode_version() const noexcept { return static_cast<int>(cre_regex_unicode_version(re_.get())); }

    // The pattern as it was given (as UTF-8).
    std::string_view source() const noexcept { return sv(cre_regex_source(re_.get())); }

    // The flags as RegExp.prototype.flags writes them, in the order dgimsuvy.
    std::string_view flags() const noexcept { return sv(cre_regex_flags(re_.get())); }

    // The number of capturing groups (a match has one more, the whole match).
    std::size_t group_count() const noexcept { return cre_regex_group_count(re_.get()); }

    // The distinct names of the groups, in the order of the first group of each name.
    std::vector<std::string_view> group_names() const {
        std::vector<std::string_view> names;
        std::size_t n = cre_regex_group_name_count(re_.get());
        names.reserve(n);
        for (std::size_t i = 0; i < n; ++i) names.push_back(sv(cre_regex_group_name(re_.get(), i)));
        return names;
    }

    // Whether the pattern matches at or after start (at start alone with the y flag), as RegExp.prototype.test with
    // lastIndex set to start.
    template <detail::text T>
    bool is_match(const T& text, std::size_t start = 0) const {
        auto t = detail::view(text);
        bool matched = false;
        cre_status status;
        if constexpr (detail::is_utf8_unit<detail::unit_of<T>>) {
            status = start == 0 ? cre_regex_is_match(re_.get(), detail::bytes(t), t.size(), &matched)
                                : cre_regex_is_match_at(re_.get(), detail::bytes(t), t.size(), start, &matched);
        } else {
            status = start == 0 ? cre_regex_is_match_utf16(re_.get(), detail::units(t), t.size(), &matched)
                                : cre_regex_is_match_at_utf16(re_.get(), detail::units(t), t.size(), start, &matched);
        }
        detail::check(status);
        return matched;
    }

    // is_match from the start of the text, with a limit on the time the match may take in place of the regex's
    // timeout: errc::match_limit when it is reached.
    template <detail::text T>
    bool is_match(const T& text, std::chrono::milliseconds limit) const {
        auto t = detail::view(text);
        bool matched = false;
        cre_status status;
        if constexpr (detail::is_utf8_unit<detail::unit_of<T>>) {
            status = cre_regex_is_match_limit(re_.get(), detail::bytes(t), t.size(), detail::millis(limit), &matched);
        } else {
            status = cre_regex_is_match_limit_utf16(re_.get(), detail::units(t), t.size(), detail::millis(limit),
                                                    &matched);
        }
        detail::check(status);
        return matched;
    }

    // The first match at or after start, as exec finds it with lastIndex set to start, or nothing.
    template <detail::text T>
    std::optional<match> find(const T& text, std::size_t start = 0) const {
        return find_impl(detail::view(text), start, 0);
    }

    // find with a limit on the time the match may take: errc::match_limit when it is reached.
    template <detail::text T>
    std::optional<match> find(const T& text, std::size_t start, std::chrono::milliseconds limit) const {
        return find_impl(detail::view(text), start, detail::millis(limit));
    }

    // The first match at or after start with what each group captured, or nothing.
    template <detail::text T>
    std::optional<basic_captures<detail::unit_of<T>>> captures(const T& text, std::size_t start = 0) const {
        return captures_impl(detail::view(text), start, 0);
    }

    // captures with a limit on the time the match may take: errc::match_limit when it is reached.
    template <detail::text T>
    std::optional<basic_captures<detail::unit_of<T>>> captures(const T& text, std::size_t start,
                                                                std::chrono::milliseconds limit) const {
        return captures_impl(detail::view(text), start, detail::millis(limit));
    }

    // The groups of the first match at or after start into a buffer the caller owns: group 0 is the match, then each
    // capturing group (false for one that did not take part). A buffer shorter than group_count() + 1 takes the first
    // groups. Nothing is allocated once the thread has read a match with as many groups. Whether there was a match.
    template <detail::text T>
    bool captures_read(const T& text, std::size_t start, std::span<match> groups) const {
        auto t = detail::view(text);
        bool found = false;
        auto* out = reinterpret_cast<cre_match*>(groups.data());
        if constexpr (detail::is_utf8_unit<detail::unit_of<T>>) {
            detail::check(
                cre_regex_captures_read(re_.get(), detail::bytes(t), t.size(), start, out, groups.size(), &found));
        } else {
            detail::check(cre_regex_captures_read_utf16(re_.get(), detail::units(t), t.size(), start, out,
                                                        groups.size(), &found));
        }
        return found;
    }

    // Every match of a global search of the text, as a range of match (see basic_match_range). The text must outlive
    // the range.
    template <detail::text T>
    basic_match_range<detail::unit_of<T>> find_all(const T& text) const {
        auto t = detail::view(text);
        cre_matches* it = nullptr;
        if constexpr (detail::is_utf8_unit<detail::unit_of<T>>) {
            detail::check(cre_regex_find_iter(re_.get(), detail::bytes(t), t.size(), &it));
        } else {
            detail::check(cre_regex_find_iter_utf16(re_.get(), detail::units(t), t.size(), &it));
        }
        return basic_match_range<detail::unit_of<T>>(re_, it);
    }

    // Every match of a global search of the text with its groups, as a range of basic_captures. The text must outlive
    // the range.
    template <detail::text T>
    basic_captures_range<detail::unit_of<T>> captures_all(const T& text) const {
        auto t = detail::view(text);
        auto handle = new_captures();
        cre_matches* it = nullptr;
        if constexpr (detail::is_utf8_unit<detail::unit_of<T>>) {
            detail::check(cre_regex_captures_iter(re_.get(), detail::bytes(t), t.size(), &it));
        } else {
            detail::check(cre_regex_captures_iter_utf16(re_.get(), detail::units(t), t.size(), &it));
        }
        return basic_captures_range<detail::unit_of<T>>(re_, it,
                                                        basic_captures<detail::unit_of<T>>(std::move(handle), t));
    }

    // The text with the first match replaced, or every match with the g flag, as String.prototype.replace replaces
    // them. The replacement is a template: $& is the match, $` the text before it, $' the text after it, $1 to $99 a
    // group, $<name> a named group, and $$ a dollar sign. Over UTF-8, a half character (an end between the halves of a
    // character beyond the Basic Multilingual Plane, without u or v) is written as U+FFFD.
    template <detail::text T, detail::text R>
        requires std::is_same_v<detail::unit_of<T>, detail::unit_of<R>>
    std::basic_string<detail::unit_of<T>> replace(const T& text, const R& replacement) const {
        return replace_impl(detail::view(text), detail::view(replacement), false);
    }

    // replace with every match replaced, whether or not the g flag is set (String.prototype.replaceAll).
    template <detail::text T, detail::text R>
        requires std::is_same_v<detail::unit_of<T>, detail::unit_of<R>>
    std::basic_string<detail::unit_of<T>> replace_all(const T& text, const R& replacement) const {
        return replace_impl(detail::view(text), detail::view(replacement), true);
    }

    // The text split around the matches, as String.prototype.split(separator, limit) splits it: what each group
    // captured is spliced in after the piece before it (nothing for a group that did not take part), an empty match
    // at the start or the end of the text splits nothing, and there are at most limit pieces.
    template <detail::text T>
    std::vector<std::optional<std::basic_string<detail::unit_of<T>>>> split(const T& text,
                                                                           std::uint32_t limit = no_limit) const {
        using C = detail::unit_of<T>;
        auto t = detail::view(text);
        std::vector<std::optional<std::basic_string<C>>> out;
        if constexpr (detail::is_utf8_unit<C>) {
            cre_pieces pieces{};
            detail::check(cre_regex_split(re_.get(), detail::bytes(t), t.size(), limit, &pieces));
            out.reserve(pieces.len);
            for (std::size_t i = 0; i < pieces.len; ++i) {
                const cre_str& p = pieces.items[i];
                if (p.ptr == nullptr) {
                    out.emplace_back();
                } else {
                    out.emplace_back(std::in_place, reinterpret_cast<const C*>(p.ptr), p.len);
                }
            }
            cre_pieces_free(&pieces);
        } else {
            cre_pieces16 pieces{};
            detail::check(cre_regex_split_utf16(re_.get(), detail::units(t), t.size(), limit, &pieces));
            out.reserve(pieces.len);
            for (std::size_t i = 0; i < pieces.len; ++i) {
                const cre_str16& p = pieces.items[i];
                if (p.ptr == nullptr) {
                    out.emplace_back();
                } else {
                    out.emplace_back(std::in_place, reinterpret_cast<const C*>(p.ptr), p.len);
                }
            }
            cre_pieces16_free(&pieces);
        }
        return out;
    }

    // The C library's handle, for calls this header does not wrap. It stays valid as long as this regex (or a copy).
    const cre_regex* native_handle() const noexcept { return re_.get(); }

  private:
    template <class C>
    static std::string pattern_bytes(std::basic_string_view<C> pattern) {
        if constexpr (detail::is_utf8_unit<C>) {
            return std::string(detail::bytes(pattern), pattern.size());
        } else {
            return detail::utf8_of(pattern);
        }
    }

    // A regex that owns a handle the C library made.
    explicit regex(cre_regex* re) { own(re); }

    void own(cre_regex* re) {
        re_ = std::shared_ptr<const cre_regex>(re, [](const cre_regex* r) { cre_regex_free(const_cast<cre_regex*>(r)); });
    }

    // Compiles with every setting given: the C library's status, and on CRE_OK the handle in *re. No timeout takes
    // the default timeout.
    template <class C>
    static cre_status new_regex(std::basic_string_view<C> pattern, std::string_view flags, const options& opts,
                                cre_regex** re) {
        std::string p = pattern_bytes(pattern);
        std::uint64_t ms = opts.match_timeout ? detail::timeout_millis(*opts.match_timeout) : cre_default_match_timeout();
        return cre_regex_new_with_options(p.data(), p.size(), flags.data(), flags.size(), ms,
                                          static_cast<std::uint32_t>(opts.edition),
                                          static_cast<std::uint32_t>(opts.unicode_version), re);
    }

    template <class C>
    void compile(std::basic_string_view<C> pattern, std::string_view flags, const options& opts) {
        cre_regex* re = nullptr;
        detail::check(new_regex(pattern, flags, opts, &re));
        own(re);
    }

    static std::string_view sv(cre_str s) noexcept { return s.ptr ? std::string_view(s.ptr, s.len) : std::string_view(); }

    std::unique_ptr<cre_captures, detail::captures_deleter> new_captures() const {
        cre_captures* c = nullptr;
        detail::check(cre_captures_new(re_.get(), &c));
        return std::unique_ptr<cre_captures, detail::captures_deleter>(c);
    }

    template <class C>
    std::optional<match> find_impl(std::basic_string_view<C> t, std::size_t start, std::uint64_t limit) const {
        cre_match m{npos, npos};
        bool found = false;
        if constexpr (detail::is_utf8_unit<C>) {
            detail::check(limit ? cre_regex_find_limit(re_.get(), detail::bytes(t), t.size(), start, limit, &m, &found)
                                : cre_regex_find(re_.get(), detail::bytes(t), t.size(), start, &m, &found));
        } else {
            detail::check(limit ? cre_regex_find_limit_utf16(re_.get(), detail::units(t), t.size(), start, limit, &m,
                                                             &found)
                                : cre_regex_find_utf16(re_.get(), detail::units(t), t.size(), start, &m, &found));
        }
        if (!found) return std::nullopt;
        return match{m.start, m.end};
    }

    template <class C>
    std::optional<basic_captures<C>> captures_impl(std::basic_string_view<C> t, std::size_t start,
                                                   std::uint64_t limit) const {
        auto handle = new_captures();
        bool found = false;
        if constexpr (detail::is_utf8_unit<C>) {
            detail::check(limit ? cre_regex_captures_limit(re_.get(), detail::bytes(t), t.size(), start, limit,
                                                           handle.get(), &found)
                                : cre_regex_captures(re_.get(), detail::bytes(t), t.size(), start, handle.get(),
                                                     &found));
        } else {
            detail::check(limit ? cre_regex_captures_limit_utf16(re_.get(), detail::units(t), t.size(), start, limit,
                                                                 handle.get(), &found)
                                : cre_regex_captures_utf16(re_.get(), detail::units(t), t.size(), start,
                                                           handle.get(), &found));
        }
        if (!found) return std::nullopt;
        return basic_captures<C>(std::move(handle), t);
    }

    template <class C>
    std::basic_string<C> replace_impl(std::basic_string_view<C> t, std::basic_string_view<C> r, bool all) const {
        if constexpr (detail::is_utf8_unit<C>) {
            cre_string s{};
            cre_status status =
                all ? cre_regex_replace_all(re_.get(), detail::bytes(t), t.size(), detail::bytes(r), r.size(), &s)
                    : cre_regex_replace(re_.get(), detail::bytes(t), t.size(), detail::bytes(r), r.size(), &s);
            detail::check(status);
            std::basic_string<C> out(reinterpret_cast<const C*>(s.ptr), s.len);
            cre_string_free(&s);
            return out;
        } else {
            cre_string16 s{};
            cre_status status = all ? cre_regex_replace_all_utf16(re_.get(), detail::units(t), t.size(),
                                                                  detail::units(r), r.size(), &s)
                                    : cre_regex_replace_utf16(re_.get(), detail::units(t), t.size(),
                                                              detail::units(r), r.size(), &s);
            detail::check(status);
            std::basic_string<C> out(reinterpret_cast<const C*>(s.ptr), s.len);
            cre_string16_free(&s);
            return out;
        }
    }

    std::shared_ptr<const cre_regex> re_;
};

// The version of the C library this program runs with, as "major.minor.patch".
inline std::string_view library_version() noexcept {
    cre_str v = cre_version();
    return std::string_view(v.ptr, v.len);
}

}  // namespace corvus::ecma262

template <>
struct std::is_error_code_enum<corvus::ecma262::errc> : std::true_type {};

#endif  // CORVUS_REGEX_ECMA262_HPP
