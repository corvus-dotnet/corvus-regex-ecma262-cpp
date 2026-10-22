#!/usr/bin/env bash
# The C++ benchmark harness, which the central benchmark site runs each night (as the image, Dockerfile) and anyone
# can run: it builds this library's tools (the C library from its release archive), works out the compliance of each
# ECMA-262 engine over the test data, runs every engine over the Industry workloads under the interleaved protocol, and
# writes the run's one data file in the results page's shape.
#
#   tools/bench/run.sh <workloads.json> <texts directory> <out.json> [test data directory]
#
# It needs a C++20 compiler, CMake, pkg-config with RE2, Python 3 and Node 24 on PATH. ROUNDS, SECONDS_EACH, PASSES,
# SPREAD and JOB=short are the protocol's (tools/benchmarks/protocol.sh); CORES runs the harness under taskset, SETTLE
# waits that many seconds before it, SKIP_BUILD=1 takes the tools already built (the image's), RUN_URL names the run,
# and KEEP copies the run's files to a directory. COMPETITOR_COMPLIANCE=1 works out the compliance of std::regex,
# Boost.Regex and SRELL again (an hour or more, since many records take them to their limits) rather than take
# tools/bench/compliance-*.txt.
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
abs() { mkdir -p "$(dirname "$1")"; echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
workloads="$(abs "${1:?workloads.json}")"
texts="$(mkdir -p "${2:?texts directory}" && cd "$2" && pwd)"
out_file="$(abs "${3:?out.json}")"
data="${4:-}"
. "$here/tools/benchmarks/protocol.sh"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
timed() { if [ -n "${CORES:-}" ]; then taskset -c "$CORES" "$@"; else "$@"; fi; }
cd "$here"
if [ -z "$data" ]; then
  git clone --quiet --depth 1 https://github.com/corvus-dotnet/ecma262-regex-test-data "$work/test-data"
  data="$work/test-data/data"
fi
bin="${CRE_TOOLS_BIN:-$here/build/bench/tools}"
if [ "${SKIP_BUILD:-0}" != 1 ]; then
  cmake -S . -B build/bench -DCMAKE_BUILD_TYPE=Release -DCRE_CPP_TOOLS=ON -DCRE_CPP_BENCH=ON -DCORVUS_REGEX_ECMA262_CPP_TESTS=OFF
  cmake --build build/bench -j 4
fi
"$bin/dataset" "$data" "$work/wrong-corvus.tsv" > "$work/compliance-corvus.txt" 2>&1 || true
# V8=0 (the central run, for every image but one): no V8, whose counts the workloads carry (v8Count).
v8=(--v8 tools/benchmarks/v8/industry.mjs)
if [ "${V8:-1}" = 0 ]; then
  v8=()
else
  node tools/benchmarks/v8/dataset.mjs "$data" > "$work/compliance-v8.txt" 2>&1 || true
fi
for engine in std boost srell; do
  if [ "${COMPETITOR_COMPLIANCE:-0}" = 1 ]; then
    "$bin/dataset" "$data" "$work/wrong-$engine.tsv" --engine "$engine" --isolate on --record-seconds 10 \
      > "$work/compliance-$engine.txt" 2>&1 || true
  else
    cp "tools/bench/compliance-$engine.txt" "$work/compliance-$engine.txt"
  fi
done
tail -n 1 "$work"/compliance-*.txt
if [ "${SETTLE:-0}" -gt 0 ]; then
  echo "settling for $SETTLE s"
  sleep "$SETTLE"
fi
export CORVUS_REGEX_TEXTS="$texts"
timed "$bin/industry" "$workloads" "${v8[@]}" --rounds "$rounds" --seconds "$seconds" \
  --passes "$passes" --spread "$spread" --out "$work/run.json" 2> "$work/run.log"
grep -E '^(SPREAD|COUNT) ' "$work/run.log" || true
python3 tools/bench/industry-results.py --workloads "$workloads" --run "$work/run.json" --out "$out_file" \
  --compliance-dir "$work" --run-url "${RUN_URL:-}"
if [ -n "${KEEP:-}" ]; then mkdir -p "$KEEP" && cp "$work"/*.json "$work"/*.txt "$work"/*.log "$KEEP"/; fi
