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
# and KEEP copies the run's files to a directory. OTHER_ENGINES_COMPLIANCE=1 works out the compliance of std::regex and
# SRELL again (an hour or more, since many records take them to their limits) rather than take
# tools/bench/compliance-*.json. Boost.Regex implements another dialect (its ECMAScript option is its Perl syntax), so
# it carries "not ECMA-262", as RE2 does.
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
# COUNT_GATE=1 (the harness-image job): every workload counted by this library's engines beside V8, one round of the
# least timing, each count checked against V8's by count-gate.py; nothing else runs.
if [ "${COUNT_GATE:-0}" = 1 ]; then
  export CORVUS_REGEX_TEXTS="$texts"
  "$bin/industry" "$workloads" --engines corvus-utf8,corvus-utf16,v8 --v8 tools/benchmarks/v8/industry.mjs --rounds 1 --seconds 0.01 \
    --passes 1 --out "$work/counts.json" 2> "$work/counts.log" || true
  python3 tools/bench/count-gate.py "$workloads" "$work/counts.json" corvus-utf8 corvus-utf16
  exit $?
fi
# Each runner writes its engine's compliance file (--compliance): the counts, the runner and the calls it makes, and
# what it leaves out, which the results script gives the page as it is.
"$bin/dataset" "$data" "$work/wrong-corvus.tsv" --compliance "$work/compliance-corvus.json" > "$work/dataset-corvus.txt" 2>&1 || true
# V8=0 (the central run, for every image but one): no V8, whose counts the workloads carry (v8Count).
v8=(--v8 tools/benchmarks/v8/industry.mjs)
if [ "${V8:-1}" = 0 ]; then
  v8=()
else
  node tools/benchmarks/v8/dataset.mjs "$data" --compliance "$work/compliance-v8.json" > "$work/dataset-v8.txt" 2>&1 || true
fi
for engine in std srell; do
  if [ "${OTHER_ENGINES_COMPLIANCE:-0}" = 1 ]; then
    # SRELL's Unicode tables are 18.0: it is asked the data's answers for that version (the test data's overlay,
    # applied by its unicode.js, which a clone of the data has beside data/).
    asked="$data"
    if [ "$engine" = srell ]; then
      node "$data/../unicode.js" apply 18.0 "$work/data-unicode-18.0" "$data" > /dev/null
      asked="$work/data-unicode-18.0"
    fi
    "$bin/dataset" "$asked" "$work/wrong-$engine.tsv" --engine "$engine" --isolate on \
      --compliance "$work/compliance-$engine.json" > "$work/dataset-$engine.txt" 2>&1 || true
    # The commit of the data, when it is a checkout, goes in the file as dataCommit, as the checked-in file's does.
    if commit=$(git -C "$data/.." rev-parse HEAD 2>/dev/null) && [ -f "$work/compliance-$engine.json" ]; then
      python3 -c 'import json, sys; c = json.load(open(sys.argv[1])); c["dataCommit"] = sys.argv[2]; json.dump(c, open(sys.argv[1], "w"))' \
        "$work/compliance-$engine.json" "$commit"
    fi
  else
    cp "tools/bench/compliance-$engine.json" "$work/compliance-$engine.json" 2>/dev/null || true
  fi
done
# Each runner's line of counts. A runner that failed, or a checked-in compliance file that is missing, writes none:
# the timing still runs and the results script writes that engine without a compliance, so the night keeps every
# figure, and this says in its log why the engine has no compliance.
for name in corvus std srell $([ -f "$work/compliance-v8.json" ] && echo v8); do
  f="$work/compliance-$name.json"
  if [ -s "$f" ]; then
    echo "$name: $(tail -n 1 "$work/dataset-$name.txt" 2>/dev/null || echo 'checked in')"
  else
    echo "WARNING: compliance-$name.json is missing, so $name has no compliance in this run's data file"
    if [ -f "$work/dataset-$name.txt" ]; then tail -n 20 "$work/dataset-$name.txt" | sed 's/^/  /'; fi
  fi
done
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
