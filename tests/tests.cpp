// The library's tests: each member of corvus::ecma262::regex over UTF-8 and UTF-16, against answers V8 gives.
#include <corvus/regex_ecma262.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace re = corvus::ecma262;

static int failures = 0;
static int checks = 0;

#define CHECK(x)                                                                \
    do {                                                                        \
        ++checks;                                                               \
        if (!(x)) {                                                             \
            ++failures;                                                         \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); \
        }                                                                       \
    } while (0)

// Whether f throws a regex_error with the reason.
static bool throws(re::errc reason, const std::function<void()>& f) {
    try {
        f();
    } catch (const re::regex_error& e) {
        return e.reason() == reason;
    }
    return false;
}

static void compiling() {
    re::regex r(R"((?<year>\d{4})-(?<month>\d{2}))", "gu");
    CHECK(r.source() == R"((?<year>\d{4})-(?<month>\d{2}))");
    CHECK(r.flags() == "gu");
    CHECK(r.group_count() == 2);
    auto names = r.group_names();
    CHECK(names.size() == 2 && names[0] == "year" && names[1] == "month");

    // The flags come back in the order RegExp.prototype.flags writes them.
    CHECK(re::regex("a", "yig").flags() == "giy");

    CHECK(throws(re::errc::invalid_pattern, [] { re::regex("(", ""); }));
    CHECK(throws(re::errc::invalid_pattern, [] { re::regex("\\p{Nope}", "u"); }));
    CHECK(throws(re::errc::invalid_flags, [] { re::regex("a", "uv"); }));
    CHECK(throws(re::errc::invalid_flags, [] { re::regex("a", "q"); }));
    CHECK(throws(re::errc::pattern_too_large, [] { re::regex(std::string(CRE_MAX_NESTING + 1, '(') +
                                                              std::string(CRE_MAX_NESTING + 1, ')')); }));

    // The error says why, as a std::error_code too.
    try {
        re::regex("(", "u");
        CHECK(false);
    } catch (const re::regex_error& e) {
        CHECK(e.code() == re::errc::invalid_pattern);
        CHECK(e.code().category().name() == std::string("corvus::ecma262"));
        CHECK(std::string(e.what()).size() > 0);
    }

    CHECK(re::regex::is_valid("a+", "u"));
    CHECK(!re::regex::is_valid("a{2,1}", "u"));
    // Annex B reads {2,1 as text without u, and \c as a backslash then c.
    CHECK(re::regex::is_valid("a{2,1", ""));
    CHECK(!re::regex::is_valid("a{2,1", "u"));

    // A pattern given as UTF-16 is the same pattern; a lone surrogate in it is read as U+FFFD.
    re::regex r16(u"é+", "u");
    CHECK(r16.source() == "é+");
    CHECK(r16.is_match("café"));

    // A copy shares the compiled pattern.
    re::regex copy = r;
    CHECK(copy.native_handle() == r.native_handle());
}

static void matching_utf8() {
    re::regex r("o+", "");
    CHECK(r.is_match("foo"));
    CHECK(!r.is_match("bar"));
    CHECK(r.is_match(std::string("foo")));
    CHECK(r.is_match(std::string_view("foo")));

    auto m = r.find("a foo, a boo", 3);
    CHECK(m && m->start == 3 && m->end == 5);
    CHECK(m->in(std::string_view("a foo, a boo")) == "oo");
    m = r.find("a foo, a boo", 5);
    CHECK(m && m->start == 10 && m->end == 12);
    CHECK(!r.find("a foo", 5));

    // The y flag: a match must start where the search starts.
    re::regex sticky("o", "y");
    CHECK(sticky.is_match("foo", 1));
    CHECK(!sticky.is_match("foo", 0));
    CHECK(!sticky.find("xo", 0));

    // Text that is not UTF-8 is refused, with the offset of the first bad byte.
    try {
        r.is_match(std::string_view("ab\xff", 3));
        CHECK(false);
    } catch (const re::regex_error& e) {
        CHECK(e.reason() == re::errc::invalid_utf8);
        CHECK(e.offset() == 2);
    }

    // A start inside a character is not a position.
    CHECK(throws(re::errc::invalid_argument, [&] { r.find("éo", 1); }));

    // char8_t text is UTF-8 too.
    CHECK(r.is_match(u8"foo"));
    auto m8 = r.find(std::u8string_view(u8"éoo"));
    CHECK(m8 && m8->start == 2 && m8->end == 4);

    // Without u, a character beyond the Basic Multilingual Plane is two units, so . matches half of one; the position
    // between the halves is the offset of the third byte.
    re::regex half("^.", "");
    auto h = half.find("\U0001F600");
    CHECK(h && h->start == 0 && h->end == 2);
    re::regex whole("^.", "u");
    auto w = whole.find("\U0001F600");
    CHECK(w && w->start == 0 && w->end == 4);
}

static void matching_utf16() {
    re::regex r("o+", "");
    std::u16string text = u"a foo, a boo";
    auto m = r.find(text);
    CHECK(m && m->start == 3 && m->end == 5);
    CHECK(r.is_match(u"foo"));
    CHECK(!r.is_match(u"bar"));

    // UTF-16 text may hold lone surrogates; without u each is a unit, with u a code point of its own.
    std::u16string lone = u"a";
    lone += char16_t(0xD800);
    lone += u"b";
    re::regex any3("^...$", "");
    CHECK(any3.is_match(lone));
    re::regex any3u("^...$", "u");
    CHECK(any3u.is_match(lone));
    // A pair is one code point with u, two units without.
    re::regex dot2("^..$", "");
    re::regex dot1u("^.$", "u");
    CHECK(dot2.is_match(u"\U0001F600"));
    CHECK(dot1u.is_match(u"\U0001F600"));

#ifdef _WIN32
    // On Windows wchar_t text is UTF-16.
    CHECK(r.is_match(L"foo"));
#endif
}

static void groups() {
    re::regex r(R"((?<year>\d{4})-(?<month>\d{2})(-(\d{2}))?)", "u");
    auto c = r.captures("due 2026-10, then");
    CHECK(c.has_value());
    CHECK(c->size() == 5);
    CHECK(c->str(0) == "2026-10");
    CHECK(c->str("year") == "2026");
    CHECK(c->str("month") == "10");
    CHECK(c->str(1) == "2026");
    CHECK(!c->str(3));
    CHECK(!(*c)[4]);
    CHECK(!c->str("nope"));
    CHECK(c->whole().start == 4 && c->whole().end == 11);
    CHECK(!r.captures("no date"));

    // Two groups in separate alternatives may share a name: the one that took part is the name's.
    re::regex dup("(?<d>a)|(?<d>b)", "u");
    auto cb = dup.captures("b");
    CHECK(cb && cb->str("d") == "b");

    // Over UTF-16.
    auto c16 = r.captures(u"on 2027-01-31");
    CHECK(c16 && c16->str("month") == u"01" && c16->str(4) == u"31");

    // Into a buffer the caller owns.
    std::vector<re::match> buffer(r.group_count() + 1);
    CHECK(r.captures_read("2026-10-21", 0, buffer));
    CHECK(buffer[0] == (re::match{0, 10}));
    CHECK(buffer[4] == (re::match{8, 10}));
    std::vector<re::match> short_buffer(2);
    CHECK(r.captures_read(u"x 2026-10", 0, short_buffer));
    CHECK(short_buffer[1] == (re::match{2, 6}));
    CHECK(!r.captures_read("nothing", 0, buffer));
}

static void global_search() {
    re::regex r(R"(\d+)", "g");
    std::string text = "1 22 333 4444";
    std::vector<std::string> found;
    for (re::match m : r.find_all(text)) found.emplace_back(m.in(std::string_view(text)));
    CHECK((found == std::vector<std::string>{"1", "22", "333", "4444"}));

    // More matches than one batch holds.
    std::string many;
    for (int i = 0; i < 1000; ++i) many += "a ";
    re::regex a("a", "g");
    std::size_t n = 0;
    std::size_t last = 0;
    bool ordered = true;
    for (re::match m : a.find_all(many)) {
        ordered = ordered && (n == 0 || m.start > last);
        last = m.start;
        ++n;
    }
    CHECK(n == 1000);
    CHECK(ordered);
    CHECK(a.find_all(many).to_vector().size() == 1000);

    // reset() starts the same search again, part read or read to its end.
    auto again = a.find_all(many);
    auto first = again.begin();
    CHECK(first != std::default_sentinel && (*first).start == 0);
    again.reset();
    CHECK(again.to_vector().size() == 1000);
    again.reset();
    CHECK(again.to_vector().size() == 1000);

    // After an empty match the search goes on one code point later with u, one code unit without.
    re::regex empty("", "gu");
    CHECK(empty.find_all("\U0001F600").to_vector().size() == 2);
    CHECK(empty.find_all(u"\U0001F600").to_vector().size() == 2);
    re::regex empty_units("", "g");
    CHECK(empty_units.find_all(u"\U0001F600").to_vector().size() == 3);

    // The y flag: each match starts where the one before ended.
    re::regex sticky("a", "gy");
    CHECK(sticky.find_all("aab").to_vector().size() == 2);
    CHECK(sticky.find_all("baa").to_vector().empty());

    // A global search is a view, so the range adaptors take it.
    auto first_two = r.find_all(text) | std::views::take(2);
    std::size_t taken = 0;
    for (re::match m : first_two) {
        (void)m;
        ++taken;
    }
    CHECK(taken == 2);
    static_assert(std::ranges::input_range<re::match_range>);
    static_assert(std::ranges::view<re::match_range>);

    // Over UTF-16.
    std::u16string t16 = u"x1y22z333";
    std::vector<re::match> m16 = r.find_all(t16).to_vector();
    CHECK(m16.size() == 3 && m16[2] == (re::match{6, 9}));

    // With the groups.
    re::regex kv(R"((?<k>\w+)=(?<v>\w*))", "g");
    std::string pairs = "a=1 b= c=33";
    std::vector<std::pair<std::string, std::string>> got;
    for (const re::captures& c : kv.captures_all(pairs)) got.emplace_back(*c.str("k"), *c.str("v"));
    CHECK((got == std::vector<std::pair<std::string, std::string>>{{"a", "1"}, {"b", ""}, {"c", "33"}}));
    std::size_t groups16 = 0;
    for (const re::u16captures& c : kv.captures_all(u"x=1 y=2")) groups16 += c.size();
    CHECK(groups16 == 6);
    static_assert(std::ranges::input_range<re::captures_range>);
}

static void replacing() {
    re::regex date(R"((?<y>\d{4})-(?<m>\d{2}))", "g");
    CHECK(date.replace("2026-10, 2027-01", "$<m>/$1") == "10/2026, 01/2027");
    re::regex first(R"(\d+)", "");
    CHECK(first.replace("1 2 3", "[$&]") == "[1] 2 3");
    CHECK(first.replace_all("1 2 3", "[$&]") == "[1] [2] [3]");
    CHECK(first.replace("a1b", "<$`|$'|$$>") == "a<a|b|$>b");
    CHECK(first.replace(u"a1b", u"<$&>") == u"a<1>b");
    CHECK(first.replace(u8"a1b", u8"<$&>") == u8"a<1>b");

    // Over UTF-8, a half character is written as U+FFFD.
    re::regex half("^.", "");
    CHECK(half.replace("\U0001F600", "x") == "x\xEF\xBF\xBD");
}

static void splitting() {
    re::regex comma(R"(\s*(;)\s*)", "");
    auto parts = comma.split("a ; b;c");
    CHECK(parts.size() == 5);
    CHECK(parts[0] == "a" && parts[1] == ";" && parts[2] == "b" && parts[3] == ";" && parts[4] == "c");
    CHECK(comma.split("a;b;c", 2).size() == 2);

    // A group that did not take part is spliced in as nothing (undefined).
    re::regex opt("(x)?,", "");
    auto o = opt.split("a,b");
    CHECK(o.size() == 3 && o[0] == "a" && !o[1] && o[2] == "b");

    auto p16 = comma.split(u"a;b");
    CHECK(p16.size() == 3 && p16[2] == u"b");
}

static void limits() {
    // A backreference makes backtracking exponential, so a limit of time is reached.
    re::regex slow(R"(^(a+)+\1$)", "");
    std::string text(64, 'a');
    text += '!';
    CHECK(throws(re::errc::match_limit, [&] { slow.is_match(text, std::chrono::milliseconds(5)); }));
    CHECK(throws(re::errc::match_limit, [&] { slow.find(text, 0, std::chrono::milliseconds(5)); }));
    CHECK(throws(re::errc::match_limit, [&] { slow.captures(text, 0, std::chrono::milliseconds(5)); }));
    // A pattern without a backreference takes time in proportion to its size times the text.
    re::regex linear(R"(^(a+)+$)", "");
    CHECK(!linear.is_match(text, std::chrono::milliseconds(1000)));
}

static void threads() {
    // A regex is immutable: threads share it.
    re::regex r(R"(\w+)", "g");
    std::string text;
    for (int i = 0; i < 2000; ++i) text += "word ";
    std::vector<std::size_t> counts(4);
    std::vector<std::thread> workers;
    for (std::size_t t = 0; t < counts.size(); ++t) {
        workers.emplace_back([&, t] {
            for (int k = 0; k < 20; ++k) counts[t] += r.find_all(text).to_vector().size();
        });
    }
    for (auto& w : workers) w.join();
    for (std::size_t c : counts) CHECK(c == 20 * 2000);
}

// Runs f on a thread with a stack of 256 KB, as a host's thread may have (std::thread cannot be given a stack size).
static void on_small_stack(void (*f)()) {
    constexpr std::size_t size = 256 * 1024;
#ifdef _WIN32
    HANDLE thread = CreateThread(nullptr, size, [](LPVOID p) -> DWORD { reinterpret_cast<void (*)()>(p)(); return 0; },
                                 reinterpret_cast<LPVOID>(f), STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    CHECK(thread != nullptr);
    if (thread == nullptr) return;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    CHECK(pthread_attr_setstacksize(&attr, size) == 0);
    pthread_t thread;
    auto start = [](void* p) -> void* {
        reinterpret_cast<void (*)()>(p)();
        return nullptr;
    };
    int created = pthread_create(&thread, &attr, start, reinterpret_cast<void*>(f));
    pthread_attr_destroy(&attr);
    CHECK(created == 0);
    if (created == 0) pthread_join(thread, nullptr);
#endif
}

static void nested_classes() {
    // With v a class holds classes, and the parser recurses once per level. A pattern of nested classes is read on the
    // library's own deep stack, not the caller's, so on a small stack the deepest nesting the library allows compiles
    // and matches, and a deeper one is refused as too large instead of overflowing the stack and ending the process.
    // This needs the C library 0.1.2: with 0.1.1 it crashes the test program.
    std::string deepest = std::string(CRE_MAX_NESTING, '[') + "a" + std::string(CRE_MAX_NESTING, ']');
    re::regex r(deepest, "v");
    CHECK(r.is_match("a"));
    CHECK(!r.is_match("b"));
    CHECK(r.is_match(u"a"));
    auto m = r.find("xa");
    CHECK(m && m->start == 1 && m->end == 2);
    CHECK(throws(re::errc::pattern_too_large, [] { re::regex(std::string(200000, '['), "v"); }));
    CHECK(!re::regex::is_valid(std::string(200000, '['), "v"));
}

int main() {
    CHECK(re::library_version().size() >= 5);
    CHECK(CORVUS_REGEX_ECMA262_CPP_VERSION_MAJOR == CRE_VERSION_MAJOR);
    CHECK(CORVUS_REGEX_ECMA262_CPP_VERSION_MINOR == CRE_VERSION_MINOR);
    compiling();
    matching_utf8();
    matching_utf16();
    groups();
    global_search();
    replacing();
    splitting();
    limits();
    threads();
    on_small_stack(nested_classes);
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
