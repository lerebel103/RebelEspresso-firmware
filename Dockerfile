###############################################################################
# RebelEspresso Firmware — Reproducible Build Environment
#
# Based on Espressif's official IDF Docker image pinned to v6.0.2.
# Use via Docker Compose or VS Code Dev Containers for a zero-setup experience.
###############################################################################
FROM espressif/idf:v6.0.2

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
# find and use the venv pip directly (path varies by IDF version).
COPY requirements.txt constraints.txt /tmp/
RUN pip_path=$(find /opt/esp/python_env -name pip -path "*/bin/pip" | head -1) && \
    echo "Using pip at: $pip_path" && \
    $pip_path install --no-cache-dir -c /tmp/constraints.txt -r /tmp/requirements.txt && \
    rm /tmp/requirements.txt /tmp/constraints.txt

# The IDF entrypoint sources export.sh automatically, so IDF_PATH, PATH,
# and the toolchain are always available without manual setup.
WORKDIR /workspace

# Default command — drop into an interactive shell with IDF ready
CMD ["/bin/bash"]
