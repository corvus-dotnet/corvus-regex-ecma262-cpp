// The engines the data-set runner judges. This library (through corvus/regex_ecma262.hpp) gives every answer through
// its own members, over UTF-16 and UTF-8. The other engines have exec alone, over UTF-16 alone, so for them the runner
// is the caller, as a JavaScript engine's own String.prototype methods are: the global search, replace
// (GetSubstitution) and split are written over their exec, as the Rust runner writes them over regress's.
#ifndef CRE_CPP_DATASET_ENGINES_HPP
#define CRE_CPP_DATASET_ENGINES_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dataset {

inline constexpr std::size_t unset = static_cast<std::size_t>(-1);

// What a test answers: yes or no, or no answer (a limit reached, or an error).
enum class result { no = 0, yes = 1, limit = -1, error = -2 };

// Why a pattern was not compiled: not valid (PARSE), over a limit of size (TOO-LARGE), or anything else (ERROR), which
// is counted as an error too.
enum class refusal { parse, too_large, error };

// A pattern compiled by an engine that has exec alone.
class exec_regex {
  public:
    virtual ~exec_regex() = default;
    // The groups, the whole match among them.
    virtual std::size_t groups() const = 0;
    // Whether the pattern was compiled with u or v, so that after an empty match the search goes on a code point.
    virtual bool unicode() const = 0;
    // The name of group g (1 and up), or empty.
    virtual std::u16string_view name(std::size_t g) const = 0;
    // exec at from: yes with out[2 * groups()] filled (unset for a group that did not take part), no, limit or error
    // (with why).
    virtual result exec(std::u16string_view text, std::size_t from, std::size_t* out, const char** why) = 0;
};

// An engine that has exec alone: compile with the flags the record asks (a flag it cannot honour is a refusal).
class exec_engine {
  public:
    virtual ~exec_engine() = default;
    virtual std::unique_ptr<exec_regex> compile(std::u16string_view pattern, std::string_view flags, refusal& why,
                                                std::string& message) = 0;
};

// The engines built into this runner, by the name --engine takes; nullptr for this library or an unknown name.
std::unique_ptr<exec_engine> make_exec_engine(std::string_view name);
// The names of the exec engines built in.
std::vector<std::string> exec_engine_names();

}  // namespace dataset

#endif
