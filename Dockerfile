FROM ubuntu:18.04

RUN apt-get update
RUN apt-get install build-essential gcc-multilib git qemu gdb vim -y
