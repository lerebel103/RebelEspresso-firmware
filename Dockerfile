###############################################################################
# RebelEspresso Firmware — Reproducible Build Environment
#
# Based on Espressif's official IDF Docker image pinned to v5.4.1.
# Use via Docker Compose or VS Code Dev Containers for a zero-setup experience.
###############################################################################
FROM espressif/idf:v5.4.1

ARG DEBIAN_FRONTEND=noninteractive

# Additional system tools useful for development
RUN apt-get update && apt-get install -y --no-install-recommends \
    zip \
    unzip \
    jq \
    && rm -rf /var/lib/apt/lists/*

# Install project-specific Python packages into IDF's Python environment.
# IDF's own constraints are already satisfied in the base image.
# The constraints file pins exact versions for reproducible builds.
# Note: during `docker build`, the IDF entrypoint hasn't run yet, so we must
# reference the venv pip directly.
ENV IDF_PYTHON_ENV_PATH=/opt/esp/python_env/idf5.4_py3.12_env
COPY requirements.txt constraints.txt /tmp/
RUN $IDF_PYTHON_ENV_PATH/bin/pip install --no-cache-dir \
    -c /tmp/constraints.txt -r /tmp/requirements.txt \
    && rm /tmp/requirements.txt /tmp/constraints.txt

# The IDF entrypoint sources export.sh automatically, so IDF_PATH, PATH,
# and the toolchain are always available without manual setup.
WORKDIR /workspace

# Default command — drop into an interactive shell with IDF ready
CMD ["/bin/bash"]
