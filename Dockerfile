FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN mkdir -p build && cd build && cmake .. && make search_server

RUN mkdir -p /app/data && cp /app/build/data/stopwords.txt /app/data/stopwords.txt

EXPOSE 10000

CMD sh -c "echo '=== /app/docs ===' && ls /app/docs && echo '=== starting server ===' && ./build/bin/search_server 10000 0.0.0.0 /app/src/config.ini 2>&1"

#CMD sh -c "echo '=== /app/docs ===' && ls /app/docs && ./build/bin/search_server 10000 0.0.0.0 /app/src/config.ini"

#CMD ["./build/bin/search_server", "10000", "0.0.0.0", "src/config.ini"]