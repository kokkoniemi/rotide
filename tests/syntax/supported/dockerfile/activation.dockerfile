FROM alpine:3.19 AS base
WORKDIR /app
COPY . .
RUN echo "hello"
CMD ["/app/run"]
