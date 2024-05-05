FROM ubuntu:22.04

RUN mkdir /microps

WORKDIR /microps

RUN apt-get update && apt-get install -y \
    build-essential \
    iproute2 \
    iputils-ping \
    net-tools \
    netcat \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

COPY . /microps
