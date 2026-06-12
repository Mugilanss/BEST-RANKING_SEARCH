FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    poppler-utils \
    antiword \
    docx2txt \
    libpq-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app/src/jwt-cpp && \
    curl -L https://raw.githubusercontent.com/Thalhammer/jwt-cpp/master/include/jwt-cpp/jwt.h \
    -o /app/src/jwt-cpp/jwt.h

WORKDIR /app

COPY . .

RUN mkdir -p build && cd build && cmake .. && make search_server

EXPOSE 10000

CMD sh -c "rm -f /app/index.bin && echo '=== /app/docs ===' && ls /app/docs && echo '=== starting server ===' && ./build/bin/search_server 10000 0.0.0.0 /app/src/config.ini 2>&1"