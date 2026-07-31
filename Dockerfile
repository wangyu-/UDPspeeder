FROM debian:bullseye-20220711 AS builder

RUN apt-get update && apt-get install -y build-essential git
COPY . /src
WORKDIR /src
RUN make -f makefile

FROM gcr.io/distroless/base-debian11
COPY --from=builder /src/speederv2 /
ENTRYPOINT ["/speederv2"]
