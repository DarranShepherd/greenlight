FROM espressif/idf:v6.0

SHELL ["/bin/bash", "-lc"]

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash-completion \
        git \
        usbutils \
    && rm -rf /var/lib/apt/lists/*

RUN printf '%s\n' \
    'if [ -f /opt/esp/idf/export.sh ]; then' \
    '  . /opt/esp/idf/export.sh >/dev/null 2>&1' \
    'fi' >> /etc/bash.bashrc

ENV IDF_PATH=/opt/esp/idf
