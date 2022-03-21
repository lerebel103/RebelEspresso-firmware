from ubuntu:20.04 as base

# taken from https://docs.espressif.com/projects/esp-idf/en/latest/get-started/linux-setup.html
LABEL maintainer=will.castelnau@gmail.com

ENV LC_ALL C.UTF-8
ENV DEBIAN_FRONTEND noninteractive

# Taken mostly from https://docs.espressif.com/projects/esp-idf/en/latest/get-started/linux-setup.html
RUN apt-get update && apt-get install -y software-properties-common apt-utils
RUN apt-add-repository -y universe && apt-get update && \
    apt-get install -y gcc git wget make libncurses-dev flex bison gperf unzip python3-dev python3-pip python3-setuptools \
                       python3-serial python3-cryptography python3-future python3-pyparsing python3-pyelftools \
                       vim curl screen sudo build-essential docker.io libusb-1.0


# this is to get the latest version of cmake
RUN apt-get install -y apt-transport-https ca-certificates gnupg wget && \
 wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null && \
 apt-add-repository https://apt.kitware.com/ubuntu/ && \
 apt-get update && \
 apt-get install -y kitware-archive-keyring && \
 apt-get install -y cmake ninja-build

# ------------------------------------------------------------------------------
#
# esp-idf
from base as firmware-builder

# Change this accordingly for the desired ESP-IDF version
ARG ESP_IDF_VERSION=v4.4

RUN mkdir /opt/esp && \
    cd /opt/esp && \
    git clone --depth=1 --recursive --shallow-submodules --branch ${ESP_IDF_VERSION} https://github.com/espressif/esp-idf.git

# Set paths
ENV IDF_PATH /opt/esp/esp-idf

# Install esp-idf dependencies
RUN  update-alternatives --install /usr/bin/python python /usr/bin/python3 10

#RUN curl -L https://github.com/protocolbuffers/protobuf/releases/download/v3.8.0/protobuf-all-3.8.0.tar.gz > /tmp/protobuf.tar.gz && \
#cd /tmp && tar xvzf protobuf.tar.gz && \
#cd protobuf-3.8.0 && \
#./configure && make -j4 install && \
#cd python && python setup.py install --cpp_implementation

# Rebuild so cache
RUN ldconfig

# Now we can load the env for building via entrypoint and run generic commands on top
COPY entrypoint.sh /opt/entrypoint.sh
RUN chmod +x /opt/entrypoint.sh
ENTRYPOINT ["/opt/entrypoint.sh"]
CMD ["/bin/bash"]


# ------------------------------------------------------------------------------
# Add working environment for user
#
ENV DEV_USER dev

# We want us to be in sudo
RUN echo "${DEV_USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Add user finally
RUN groupadd -g 1000 ${DEV_USER} && useradd -m -s /bin/bash -u 1000 -g ${DEV_USER} -Gsudo,root,adm,dialout,plugdev ${DEV_USER}
USER ${DEV_USER}
WORKDIR /home/${DEV_USER}
ENV TERM xterm-256color

USER ${DEV_USER}

# Allow dev user to work with esp-idf
RUN sudo chown -R ${DEV_USER} ${IDF_PATH}

# Install all dependencies needed to build ESP-IDF projects
RUN ${IDF_PATH}/install.sh

# ------------------------------------------------------------------------------
#
# This is used to match this Dockerfile with the image generated, as we save
# its MD5 in a file in the root dir, so we know where it came from, other than
# looking at the md5 tag on the image.
ARG MD5
RUN sudo /bin/bash -c "echo ${MD5} > /md5"
