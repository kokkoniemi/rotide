FROM alpine:3.19

# Simple RUN body is injected as a bash document.
RUN export TARGET=/opt && mkdir -p "$TARGET" && echo "done"

# RUN heredoc body is injected as bash.
RUN <<EOF
set -euo pipefail
for name in one two three; do
  echo "building $name"
done
EOF

# COPY heredoc routed to JSON by destination filename.
COPY <<CFG /app/config.json
{
  "enabled": true,
  "level": 3
}
CFG

# COPY heredoc routed to YAML by destination filename.
COPY <<YML /app/settings.yaml
service:
  name: web
  port: 8080
YML
