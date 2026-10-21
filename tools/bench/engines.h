/* The engines of the C++ Industry harness, called by its driver (industry.c, the C harness's driver): each sets up one
 * workload over one text, then times it with measure. */
#ifndef CRE_CPP_BENCH_ENGINES_H
#define CRE_CPP_BENCH_ENGINES_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*count_fn)(void *state);

/* Times a count (industry.c): warm, then passes. The median, the fastest and the slowest pass in nanoseconds per
 * count, the count, and the counts timed. */
void measure(double seconds, int passes, count_fn count, void *state, double *median, double *min, double *max,
             size_t *expected, size_t *reps_timed);

/* Runs a workload on an engine: false with error set when the pattern is refused or the engine is unknown. flags are
 * the workload's own, flags_u the same with u. */
bool engine_run(const char *engine, const char *pattern, const char *flags, const char *flags_u, const char *text,
                size_t len, double seconds, int passes, double *median, double *min, double *max, size_t *expected,
                size_t *reps, char *error, size_t error_size);

/* Whether an engine claims ECMA-262: a count that differs from V8's is then an error, not a note. */
bool engine_claims_ecma262(const char *engine);
/* The name an engine is shown by. */
const char *engine_label(const char *engine);
/* The version of an engine's library ("" when not known). */
const char *engine_version(const char *engine);
/* The compiler the harness was built with. */
const char *engine_compiler(void);

#ifdef __cplusplus
}
#endif

#endif
