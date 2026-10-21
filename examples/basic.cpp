// The README's example: compile a pattern, test it, read its groups, walk a global search, replace and split.
#include <corvus/regex_ecma262.hpp>

#include <iostream>
#include <string>

int main() {
    namespace re = corvus::ecma262;

    // The flags are those of new RegExp(pattern, flags).
    re::regex date(R"((?<year>\d{4})-(?<month>\d{2}))", "gu");

    std::cout << std::boolalpha << date.is_match("due 2026-10") << '\n';  // true

    if (auto c = date.captures("due 2026-10")) {
        std::cout << *c->str("year") << '\n';  // 2026
    }

    // Every match of a global search, as a range.
    std::string text = "2026-10, 2027-01";
    for (re::match m : date.find_all(text)) {
        std::cout << m.in(std::string_view(text)) << '\n';  // 2026-10, then 2027-01
    }

    // replace replaces every match with the g flag, and the first without it.
    std::cout << date.replace(text, "$<month>/$1") << '\n';  // 10/2026, 01/2027

    // split splices in what the groups captured.
    for (const auto& piece : re::regex(R"(\s*(;)\s*)").split("a ; b;c")) {
        std::cout << (piece ? *piece : "undefined") << '|';  // a|;|b|;|c|
    }
    std::cout << '\n';

    // UTF-16 text is matched as it is, positions in code units.
    std::u16string utf16 = u"café 2026-12";
    auto m = date.find(utf16);
    std::cout << m->start << '\n';  // 5

    // A pattern that is not valid throws, saying why.
    try {
        re::regex bad("(", "u");
    } catch (const re::regex_error& e) {
        std::cout << (e.reason() == re::errc::invalid_pattern) << '\n';  // true
    }
}
