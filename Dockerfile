# syntax=docker/dockerfile:1.7
FROM dev-env

USER dev
WORKDIR /workspace

RUN set -eux; \
    for repo in a-cmake-library the-macro-library a-memory-library; do \
        git clone --depth 1 "https://github.com/knode-ai-open-source/${repo}.git" "$repo"; \
        (cd "$repo" && ./build_install.sh); \
        rm -rf "$repo"; \
    done

COPY --chown=dev:dev . /workspace/project
RUN cd /workspace/project && ./build_install.sh

CMD ["/bin/bash"]
