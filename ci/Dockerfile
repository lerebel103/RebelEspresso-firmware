from ubuntu:20.04

# taken from https://docs.espressif.com/projects/esp-idf/en/latest/get-started/linux-setup.html
LABEL maintainer=will.castelnau@woodside.com.au

ENV LC_ALL C.UTF-8
ENV DEBIAN_FRONTEND noninteractive

# Taken mostly from https://docs.espressif.com/projects/esp-idf/en/latest/get-started/linux-setup.html
RUN apt-get update
RUN apt-get install -y software-properties-common apt-utils
RUN apt-add-repository -y universe
RUN apt-get update
RUN apt-get install -y gcc git wget make libncurses-dev flex bison gperf unzip python python-dev python3-pip python-setuptools \
                       python3-serial python-cryptography python3-future python-pyparsing python-pyelftools
RUN apt-get install -y vim curl screen sudo build-essential docker.io


# this is to get the latest version of cmake
RUN apt-get install -y apt-transport-https ca-certificates gnupg wget
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
RUN apt-add-repository https://apt.kitware.com/ubuntu/
RUN apt-get update
RUN apt-get install -y kitware-archive-keyring
RUN apt-get install -y cmake ninja-build

# ------------------------------------------------------------------------------
#
# Install xtensa toolchain
RUN curl https://dl.espressif.com/dl/xtensa-esp32-elf-gcc8_2_0-esp-2020r2-linux-amd64.tar.gz > /tmp/xtensa.tar.gz && mkdir -p /opt/esp && cd /opt/esp && tar -xzf /tmp/xtensa.tar.gz 

# esp-idf
ENV ESP_IDF_VERSION v4.1-rc
RUN cd /opt/esp && git clone --recursive --branch ${ESP_IDF_VERSION} https://github.com/espressif/esp-idf.git

# Set paths
ENV IDF_PATH /opt/esp/esp-idf

# Unfortunately the old python pip is required for ESP-IDF at present
RUN curl https://bootstrap.pypa.io/get-pip.py --output get-pip.py
RUN python2 get-pip.py && rm get-pip.py
RUN pip install -r ${IDF_PATH}/requirements.txt


RUN curl -L https://github.com/protocolbuffers/protobuf/releases/download/v3.8.0/protobuf-all-3.8.0.tar.gz > /tmp/protobuf.tar.gz && \
cd /tmp && tar xvzf protobuf.tar.gz && \
cd protobuf-3.8.0 && \
./configure && make -j4 install && \
cd python && python setup.py install --cpp_implementation

ENV PATH /opt/esp/xtensa-esp32-elf/bin:${IDF_PATH}/components/esptool_py/esptool/:${IDF_PATH}/tools:${PATH}

# Rebuild so cache
RUN ldconfig

# ------------------------------------------------------------------------------
#
# This is used to match this Dockerfile with the image generated, as we save
# its MD5 in a file in the root dir, so we know where it came from, other than
# looking at the md5 tag on the image.
ARG MD5
RUN echo ${MD5} > /md5

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
RUN sudo chown -R ${DEV_USER} /opt/esp*

