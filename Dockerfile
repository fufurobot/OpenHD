FROM ubuntu:latest
RUN apt-get update
RUN apt-get install -y sudo git
# need this to prevent git werid permission error
WORKDIR /root
# change to to avoid CMakeCache accidently changed.
RUN git clone -b release --recursive https://github.com/fufurobot/OpenHD
WORKDIR /root/OpenHD
RUN ./install_build_dep.sh ubuntu-x86
RUN sudo apt install -y python3-pip
#RUN pip install cloudsmith-api --break-system-packages
#RUN pip install cloudsmith-cli --break-system-packages
RUN sudo ./package.sh standard x86_64 ubuntu noble

