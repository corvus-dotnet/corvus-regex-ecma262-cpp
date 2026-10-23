# The C++ benchmark harness as an image: this library's tools (the C library from its release archive, the data-set
# runner and the harness, with Boost.Regex and SRELL at their pinned releases, std::regex from GCC's libstdc++ and RE2
# from Debian) and Node 24 for the V8 rows, with tools/bench/run.sh as the entry point. Built from the repository's
# root:
#
#   docker build -t corvus-regex-ecma262-cpp .
#   docker run --rm -v <workloads.json>:/workloads.json:ro -v <texts dir>:/texts:ro -v <out dir>:/out <image>
#
# which writes /out/industry.json, the run's data file in the results page's shape. The ECMA-262 test data is cloned
# at depth 1 inside the container unless its data/ directory is mounted at /data. JOB=short, SECONDS_EACH, PASSES,
# CORES, SETTLE and RUN_URL pass through (see tools/bench/run.sh). The central benchmark site runs this image each
# night; .github/workflows/harness-image.yml builds, smoke-runs and publishes it.
FROM node:24.22.0-bookworm-slim@sha256:91acba91b842bec0bf7ea6f5e09c644c7ccad72c4996ed9616d861d1c7cd60d4 AS node

FROM gcc:14@sha256:db19db55a3bd1f836212a76854c4f6c7c4ccb0c9a9426dbf542e3505bc8cdfec
RUN apt-get update && apt-get install -y --no-install-recommends cmake ninja-build pkg-config libre2-dev python3 git \
    util-linux ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=node /usr/local/bin/node /usr/local/bin/node
WORKDIR /harness
COPY . .
RUN cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DCRE_CPP_TOOLS=ON -DCRE_CPP_BENCH=ON \
      -DCORVUS_REGEX_ECMA262_CPP_TESTS=OFF \
    && cmake --build build/bench -j 4
# The commit the image is built from, for the run's record (the image has no .git).
ARG COMMIT=""
ENV SKIP_BUILD=1 CORVUS_COMMIT=$COMMIT
ENTRYPOINT ["/harness/tools/bench/container-entry.sh"]
