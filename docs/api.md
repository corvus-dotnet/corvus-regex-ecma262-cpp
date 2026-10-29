# API reference

The C++ API: the header `corvus/regex_ecma262.hpp`, namespace `corvus::ecma262`, generated from its declarations
and their comments by `tools/docs/gen-api.ps1`. [Getting started](getting-started.md) shows it at work.

## Conventions

- A regex is a compiled pattern, read as new RegExp(pattern, flags) reads it. It is immutable, so any number of
  threads may use one at once, and copying one shares the compiled pattern rather than compiling it again.
- Text is any std::basic_string_view of char or char8_t (UTF-8, checked: text that is not UTF-8 throws) or of
  char16_t (UTF-16, not checked, lone surrogates and all), and so a std::basic_string or a pointer to a
  NUL-terminated string of those. On Windows, wchar_t text is UTF-16. Nothing is transcoded: positions are in the
  units of the text, bytes of UTF-8 or UTF-16 code units.
- A match is the one exec finds. Without the u or v flag a pattern is matched by UTF-16 code unit, so over UTF-8 a
  match can start or end between the halves of a character beyond the Basic Multilingual Plane; such a position is
  the offset of the character's third byte.
- Failures throw corvus::ecma262::regex_error, whose code() says why (std::error_code, in ecma262::category()). A
  pattern that is not valid is errc::invalid_pattern; a match that reaches its limit of time or of backtracking
  memory is errc::match_limit.
- A regex has a match timeout, as a Regex of .NET has: the constructor that takes a std::chrono::milliseconds, or
  regex::set_default_match_timeout for every regex made after it (none unless set). Every member that matches gives
  each of its matches the timeout, and the overloads that take a std::chrono::milliseconds give one match a limit
  of their own. Without a timeout a match has no limit of time or of memory, so give a pattern or a text from
  outside one.
- A pattern is read in the grammar of an edition (edition::current, today's, or edition::es2020, which JSON Schema
  2020-12 names) and under a version of Unicode (unicode_version::v8, the version of the V8 the answers are held to,
  17 today; v13, v17, v18; or latest, 18 today), each independent of the other and settled when it is compiled, so
  that they cost a match nothing: regex::options carries them with a match timeout. regex::try_compile is the form
  that does not throw, and takes the options too, so a validator needs no default timeout.
- A global search (find_all, captures_all) is a range that keeps the regex alive but not the text, as a
  std::string_view does not: the text must outlive it.

## errc

```cpp
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
```

Why a call failed: the C library's statuses other than CRE_OK.

## category

```cpp
inline const std::error_category& category() noexcept;
```

The error category of errc.

## make_error_code

```cpp
inline std::error_code make_error_code(errc e) noexcept;
```

## regex_error

```cpp
class regex_error : public std::system_error
```

What a call that failed throws: the reason (code(), an errc), the C library's message (what()) and, for text that
is not UTF-8, the offset of the first invalid byte (offset()).

### regex_error::regex_error

```cpp
regex_error(errc code, const std::string& message, std::size_t offset = 0)
    : std::system_error(make_error_code(code), message), offset_(offset);
```

### regex_error::reason

```cpp
errc reason() const noexcept;
```

The reason, as an errc.

### regex_error::offset

```cpp
std::size_t offset() const noexcept;
```

The offset of the first byte that is not UTF-8, for errc::invalid_utf8; otherwise 0.

## edition

```cpp
enum class edition : std::uint32_t {
    current = CRE_EDITION_CURRENT,
    es2020 = CRE_EDITION_ES2020,
};
```

The edition of ECMA-262 whose grammar a pattern is read with: today's, the default, or ECMAScript 2020's. The 2020
grammar refuses what later editions added: the d and v flags (errc::invalid_flags), modifier groups, a name given to
more than one group, and property names that are not in its edition's tables (each errc::invalid_pattern).

## unicode_version

```cpp
enum class unicode_version : std::uint32_t {
    v8 = CRE_UNICODE_DEFAULT,
    v13 = CRE_UNICODE_13,
    v17 = CRE_UNICODE_17,
    v18 = CRE_UNICODE_18,
    latest = CRE_UNICODE_LATEST,
};
```

The version of Unicode whose tables a pattern is read with (its properties and its case folding), independent of
the edition: v8, the version of the V8 the answers are held to (17 today); 13, 17 or 18; or latest, the latest the C
library carries (18 today).

## unicode_versions

```cpp
inline constexpr std::array<int, 3> unicode_versions{13, 17, 18};

inline constexpr int default_unicode_version = 17;

inline constexpr int latest_unicode_version = 18;
```

The versions of Unicode a pattern can be read with; the one unicode_version::v8 stands for; and the one
unicode_version::latest stands for.

## npos

```cpp
inline constexpr std::size_t npos = CRE_UNSET;
```

The position of no match and of a group that did not take part.

## match

```cpp
struct match
```

Where a match or a group is in its text: start to end, in the text's units (bytes of UTF-8, or UTF-16 code
units). A group that did not take part is npos to npos, and is false.

### match::start

```cpp
std::size_t start = npos;

std::size_t end = npos;
```

### match::operator bool

```cpp
constexpr explicit operator bool() const noexcept;
```

Whether the group took part in the match.

### match::length

```cpp
constexpr std::size_t length() const noexcept;
```

The length in units (0 for a group that did not take part).

### match::in

```cpp
template <class C>
constexpr std::basic_string_view<C> in(std::basic_string_view<C> text) const noexcept;
```

The part of the text, which must be the text the match was found in.

### match::operator==

```cpp
friend constexpr bool operator==(const match&, const match&) = default;
```

## no_limit

```cpp
inline constexpr std::uint32_t no_limit = CRE_SPLIT_ALL;
```

The limit of split that keeps every piece, as split(separator) with no limit.

## basic_captures

```cpp
template <class C>
class basic_captures
```

What a match captured: each group by number (0 is the whole match) and by name, with the text it was found in.
It is what regex::captures returns, and what a captures_all range gives for each match (reused from one match to
the next, so copy what must outlive the step).

### basic_captures::size

```cpp
std::size_t size() const noexcept;
```

The number of groups, the whole match among them.

### basic_captures::operator[]

```cpp
match operator[](std::size_t i) const noexcept;

match operator[](std::string_view name) const noexcept;
```

Where group i is (false when it did not take part, or there is no such group).

Where the group of the name is (of the groups that share it, the one that took part).

### basic_captures::str

```cpp
std::optional<std::basic_string_view<C>> str(std::size_t i) const noexcept;

std::optional<std::basic_string_view<C>> str(std::string_view name) const noexcept;
```

The text of group i, or nothing when it did not take part.

The text of the group of the name, or nothing when no group of that name took part.

### basic_captures::whole

```cpp
match whole() const noexcept;
```

The whole match.

### basic_captures::text

```cpp
std::basic_string_view<C> text() const noexcept;
```

The text the match was found in.

## captures

```cpp
using captures = basic_captures<char>;

using u8captures = basic_captures<char8_t>;

using u16captures = basic_captures<char16_t>;
```

## basic_match_range

```cpp
template <class C>
class basic_match_range : public std::ranges::view_interface<basic_match_range<C>>
```

A global search of one text, as matchAll makes one, as a range of match: after an empty match the search goes on
one character later (one code point with u or v, one UTF-16 code unit without), and with the y flag each match
must start where the one before it ended. It reads the matches from the library in batches. It is an input
range: it is read once, and begin() may be called once (again after reset()).

### basic_match_range::iterator

```cpp
class iterator {
    using value_type = match;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;
    iterator() = default;
    match operator*() const noexcept;
    iterator& operator++();
    void operator++(int);
    friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept;
};
```

### basic_match_range::basic_match_range

```cpp
basic_match_range(basic_match_range&&) noexcept = default;
```

### basic_match_range::operator=

```cpp
basic_match_range& operator=(basic_match_range&&) noexcept = default;
```

### basic_match_range::begin

```cpp
iterator begin();
```

The first match. It reads the first batch.

### basic_match_range::end

```cpp
std::default_sentinel_t end() const noexcept;
```

### basic_match_range::reset

```cpp
void reset() noexcept;
```

Starts the search again at the start of its text, which is not checked again (as setting lastIndex to 0), so
that the same text can be searched again without a new search: begin() may then be called once more.

### basic_match_range::to_vector

```cpp
std::vector<match> to_vector();
```

Every match, into a vector.

## match_range

```cpp
using match_range = basic_match_range<char>;

using u8match_range = basic_match_range<char8_t>;

using u16match_range = basic_match_range<char16_t>;
```

## basic_captures_range

```cpp
template <class C>
class basic_captures_range : public std::ranges::view_interface<basic_captures_range<C>>
```

A global search as basic_match_range, keeping each match's groups: a range of basic_captures, one object reused
from match to match (so copy what must outlive the step). An input range, read once.

### basic_captures_range::iterator

```cpp
class iterator {
    using value_type = basic_captures<C>;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;
    iterator() = default;
    const basic_captures<C>& operator*() const noexcept;
    const basic_captures<C>* operator->() const noexcept;
    iterator& operator++();
    void operator++(int);
    friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept;
};
```

### basic_captures_range::basic_captures_range

```cpp
basic_captures_range(basic_captures_range&&) noexcept = default;
```

### basic_captures_range::operator=

```cpp
basic_captures_range& operator=(basic_captures_range&&) noexcept = default;
```

### basic_captures_range::begin

```cpp
iterator begin();
```

### basic_captures_range::end

```cpp
std::default_sentinel_t end() const noexcept;
```

## captures_range

```cpp
using captures_range = basic_captures_range<char>;

using u8captures_range = basic_captures_range<char8_t>;

using u16captures_range = basic_captures_range<char16_t>;
```

## regex

```cpp
class regex
```

A compiled ECMA-262 regular expression.

### regex::options

```cpp
struct options {
    std::optional<std::chrono::milliseconds> match_timeout;
    ecma262::edition edition = ecma262::edition::current;
    ecma262::unicode_version unicode_version = ecma262::unicode_version::v8;
};
```

How a pattern is compiled besides its flags: its match timeout (nullopt for the default timeout, zero for
none), the edition whose grammar it is read with, and the version of Unicode whose tables it is read with.

### regex::regex

```cpp
template <detail::text P>
explicit regex(const P& pattern, std::string_view flags = {});

template <detail::text P>
regex(const P& pattern, std::string_view flags, std::chrono::milliseconds match_timeout);

template <detail::text P>
regex(const P& pattern, std::string_view flags, const options& opts);
```

Compiles a pattern with the flags new RegExp(pattern, flags) takes, any of dgimsuvy. Throws regex_error:
errc::invalid_pattern, invalid_flags or pattern_too_large. A pattern given as UTF-16 is read as UTF-8, a lone
surrogate as U+FFFD.

As above, with a match timeout: the time each match may take before it is abandoned with errc::match_limit
(zero for none). A timeout also caps the backtracking memory of a match.

As above, with the options: under edition::es2020 the d and v flags are errc::invalid_flags.

### regex::try_compile

```cpp
template <detail::text P>
static std::optional<regex> try_compile(const P& pattern, std::string_view flags, const options& opts,
                                        std::error_code& ec);
```

The constructor that does not throw for the pattern: the regex, or nullopt with ec set to why (errc::
invalid_pattern, invalid_flags or pattern_too_large). It takes the options, so a check of a pattern from outside
gives it a timeout of its own without the default.

### regex::set_default_match_timeout

```cpp
static void set_default_match_timeout(std::optional<std::chrono::milliseconds> timeout) noexcept;
```

Sets the match timeout of every regex made from now on without one of its own, or nullopt (or zero) for none,
the default. It is the process-wide default of .NET's REGEX_DEFAULT_MATCH_TIMEOUT; a program that matches
patterns or texts from outside should set it when it starts. A regex already made keeps its own.

### regex::default_match_timeout

```cpp
static std::optional<std::chrono::milliseconds> default_match_timeout() noexcept;
```

The match timeout of a regex made now without one of its own, or nullopt for none.

### regex::match_timeout

```cpp
std::optional<std::chrono::milliseconds> match_timeout() const noexcept;
```

The time each match may take before it is abandoned with errc::match_limit, or nullopt for none.

### regex::is_valid

```cpp
template <detail::text P>
static bool is_valid(const P& pattern, std::string_view flags = {});

template <detail::text P>
static bool is_valid(const P& pattern, std::string_view flags, ecma262::edition ed, ecma262::unicode_version uv);
```

Whether a pattern is valid with the flags, without compiling it. A pattern over a limit of size is not valid
here.

As above, in the grammar of an edition and under a version of Unicode.

### regex::edition

```cpp
ecma262::edition edition() const noexcept;
```

The edition whose grammar the pattern was read with.

### regex::unicode_version

```cpp
int unicode_version() const noexcept;
```

The version of Unicode whose tables the pattern was read with, resolved: 13, 17 or 18.

### regex::source

```cpp
std::string_view source() const noexcept;
```

The pattern as it was given (as UTF-8).

### regex::flags

```cpp
std::string_view flags() const noexcept;
```

The flags as RegExp.prototype.flags writes them, in the order dgimsuvy.

### regex::group_count

```cpp
std::size_t group_count() const noexcept;
```

The number of capturing groups (a match has one more, the whole match).

### regex::group_names

```cpp
std::vector<std::string_view> group_names() const;
```

The distinct names of the groups, in the order of the first group of each name.

### regex::is_match

```cpp
template <detail::text T>
bool is_match(const T& text, std::size_t start = 0) const;

template <detail::text T>
bool is_match(const T& text, std::chrono::milliseconds limit) const;
```

Whether the pattern matches at or after start (at start alone with the y flag), as RegExp.prototype.test with
lastIndex set to start.

is_match from the start of the text, with a limit on the time the match may take in place of the regex's
timeout: errc::match_limit when it is reached.

### regex::find

```cpp
template <detail::text T>
std::optional<match> find(const T& text, std::size_t start = 0) const;

template <detail::text T>
std::optional<match> find(const T& text, std::size_t start, std::chrono::milliseconds limit) const;
```

The first match at or after start, as exec finds it with lastIndex set to start, or nothing.

find with a limit on the time the match may take: errc::match_limit when it is reached.

### regex::captures

```cpp
template <detail::text T>
std::optional<basic_captures<detail::unit_of<T>>> captures(const T& text, std::size_t start = 0) const;

template <detail::text T>
std::optional<basic_captures<detail::unit_of<T>>> captures(const T& text, std::size_t start,
                                                            std::chrono::milliseconds limit) const;
```

The first match at or after start with what each group captured, or nothing.

captures with a limit on the time the match may take: errc::match_limit when it is reached.

### regex::captures_read

```cpp
template <detail::text T>
bool captures_read(const T& text, std::size_t start, std::span<match> groups) const;
```

The groups of the first match at or after start into a buffer the caller owns: group 0 is the match, then each
capturing group (false for one that did not take part). A buffer shorter than group_count() + 1 takes the first
groups. Nothing is allocated once the thread has read a match with as many groups. Whether there was a match.

### regex::find_all

```cpp
template <detail::text T>
basic_match_range<detail::unit_of<T>> find_all(const T& text) const;
```

Every match of a global search of the text, as a range of match (see basic_match_range). The text must outlive
the range.

### regex::captures_all

```cpp
template <detail::text T>
basic_captures_range<detail::unit_of<T>> captures_all(const T& text) const;
```

Every match of a global search of the text with its groups, as a range of basic_captures. The text must outlive
the range.

### regex::replace

```cpp
template <detail::text T, detail::text R>
    requires std::is_same_v<detail::unit_of<T>, detail::unit_of<R>>
std::basic_string<detail::unit_of<T>> replace(const T& text, const R& replacement) const;
```

The text with the first match replaced, or every match with the g flag, as String.prototype.replace replaces
them. The replacement is a template: $& is the match, $\` the text before it, $' the text after it, $1 to $99 a
group, $\<name> a named group, and $$ a dollar sign. Over UTF-8, a half character (an end between the halves of a
character beyond the Basic Multilingual Plane, without u or v) is written as U+FFFD.

### regex::replace_all

```cpp
template <detail::text T, detail::text R>
    requires std::is_same_v<detail::unit_of<T>, detail::unit_of<R>>
std::basic_string<detail::unit_of<T>> replace_all(const T& text, const R& replacement) const;
```

replace with every match replaced, whether or not the g flag is set (String.prototype.replaceAll).

### regex::split

```cpp
template <detail::text T>
std::vector<std::optional<std::basic_string<detail::unit_of<T>>>> split(const T& text,
                                                                       std::uint32_t limit = no_limit) const;
```

The text split around the matches, as String.prototype.split(separator, limit) splits it: what each group
captured is spliced in after the piece before it (nothing for a group that did not take part), an empty match
at the start or the end of the text splits nothing, and there are at most limit pieces.

### regex::native_handle

```cpp
const cre_regex* native_handle() const noexcept;
```

The C library's handle, for calls this header does not wrap. It stays valid as long as this regex (or a copy).

## library_version

```cpp
inline std::string_view library_version() noexcept;
```

The version of the C library this program runs with, as "major.minor.patch".
