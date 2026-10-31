FROM debian:13-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake ninja-build g++-mingw-w64-x86-64-posix \
    qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools \
    nsis python3 python3-venv p7zip-full curl file && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install --no-cache-dir aqtinstall
RUN /opt/aqt/bin/aqt install-qt windows desktop 6.8.2 win64_mingw -O /opt/qt
