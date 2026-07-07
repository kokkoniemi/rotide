# syntax=docker/dockerfile:1
ARG NODE_VERSION=20
FROM node:${NODE_VERSION}-alpine AS builder
LABEL org.opencontainers.image.source="https://example.com/repo"
ENV APP_HOME=/srv/app NODE_ENV=production
WORKDIR ${APP_HOME}
COPY package.json .
RUN npm ci
EXPOSE 8080
USER node
STOPSIGNAL SIGTERM
ENTRYPOINT ["node", "server.js"]
