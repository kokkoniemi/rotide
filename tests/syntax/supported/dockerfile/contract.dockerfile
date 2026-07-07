# Multi-stage build exercising the full instruction set.
ARG BASE_IMAGE=debian
ARG BASE_TAG=bookworm-slim

FROM ${BASE_IMAGE}:${BASE_TAG}@sha256:abc123def456 AS build
MAINTAINER Example Team <team@example.com>
LABEL maintainer="team@example.com" \
      version="1.2.3"
ENV LANG=C.UTF-8 \
    PATH="/opt/bin:${PATH}"
WORKDIR /workspace
ADD https://example.com/archive.tar.gz /tmp/
COPY --chown=root:root src/ ./src/
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential \
    && rm -rf /var/lib/apt/lists/*
RUN <<EOF
set -eux
make all
make install
EOF

FROM ${BASE_IMAGE}:${BASE_TAG} AS runtime
ARG APP_USER=appuser
ENV HOME=/home/${APP_USER}
COPY --from=build /workspace/dist /opt/app
VOLUME ["/data"]
EXPOSE 8080 8443
USER ${APP_USER}
WORKDIR /opt/app
HEALTHCHECK --interval=30s --timeout=3s \
    CMD curl -f http://localhost:8080/health || exit 1
ONBUILD COPY . /opt/app/plugins
STOPSIGNAL SIGQUIT
ENTRYPOINT ["/opt/app/bin/server"]
CMD ["--config", "/etc/app/config.yaml"]
