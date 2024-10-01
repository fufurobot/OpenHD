FROM ubuntu:latest
RUN apt-get update
RUN apt-get install -y sudo
# need this to prevent git werid permission error
COPY . /root/OpenHD
WORKDIR /root/OpenHD
RUN ./install_build_dep.sh 

