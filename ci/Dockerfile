from ubuntu:latest

# taken from https://docs.espressif.com/projects/esp-idf/en/latest/get-started/linux-setup.html
RUN apt-get update && apt-get install -y gcc git wget make libncurses-dev flex bison gperf python python-pip python-setuptools python-serial python-cryptography python-future python-pyparsing python-pyelftools

RUN apt-get install -y vim curl screen sudo cmake build-essential

# ------------------------------------------------------------------------------
#
# Install xtensa toolchain
RUN curl https://dl.espressif.com/dl/xtensa-esp32-elf-gcc8_2_0-esp32-2019r1-linux-amd64.tar.gz > /tmp/xtensa.tar.gz && mkdir -p /opt/esp && cd /opt/esp && tar -xzf /tmp/xtensa.tar.gz 

ENV PATH /opt/esp/xtensa-esp32-elf/bin:$PATH
RUN echo "export PATH=\"/opt/esp/xtensa-esp32-elf/bin:\$PATH\"" >> /etc/bash.bashrc

# esp-idf
ENV ESP_IDF_VERSION v4.1-beta2
RUN cd /opt/esp && git clone --recursive --branch ${ESP_IDF_VERSION} https://github.com/espressif/esp-idf.git

# Set paths
ENV IDF_PATH /opt/esp/esp-idf
RUN echo "export PATH=\"${IDF_PATH}/components/esptool_py/esptool/:\$PATH\"" >> /etc/bash.bashrc
RUN echo "export PATH=\"${IDF_PATH}/tools:\$PATH\"" >> /etc/bash.bashrc

# Install python requirements
RUN python -m pip install -r ${IDF_PATH}/requirements.txt

# ------------------------------------------------------------------------------
# Add working environment for user
#
ENV DEV_USER dev

# We want us to be in sudo
RUN echo "${DEV_USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Add user finally
RUN groupadd -g 1000 ${DEV_USER} && useradd -m -s /bin/bash -u 1000 -g ${DEV_USER} -Gsudo,adm,dialout,plugdev ${DEV_USER}
USER ${DEV_USER}
WORKDIR /home/${DEV_USER}
ENV TERM xterm-256color

USER ${DEV_USER}

# Allow dev user to work with esp-idf
RUN sudo chown -R ${DEV_USER} /opt/esp*

