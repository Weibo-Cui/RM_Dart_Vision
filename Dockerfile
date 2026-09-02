# arm64v8架构基础镜像
FROM arm64v8/ubuntu:22.04

# 创建 workspace
RUN mkdir -p /dart_ws
WORKDIR /dart_ws/

# 设置非交互式环境避免卡在时区选择
ENV DEBIAN_FRONTEND=noninteractive
# 设置默认时区
ENV TZ=Asia/Shanghai

# 设置线程数
ARG THREADS=1
ENV THREADS=${THREADS}

# 更换软件源为中科大源
RUN \
    if [ -f /etc/apt/sources.list ]; then \
        sed -i \
            -e 's@//ports.ubuntu.com/@//ports.ubuntu.com/ubuntu-ports/@g' \
            -e 's@//ports.ubuntu.com@//mirrors.ustc.edu.cn@g' \
            /etc/apt/sources.list; \
    else \
        sed -i 's@//ports.ubuntu.com@//mirrors.ustc.edu.cn@g' /etc/apt/sources.list.d/ubuntu.sources; \
    fi

# apt 依赖
RUN \
    apt update && apt install -y \
    nano htop locales curl lsb-release \
    # 编译工具
    git cmake build-essential clangd g++ \
    # openvino
    wget gnupg software-properties-common \
    bash-completion  && \
    apt clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* && \
    # 设置语言环境
    locale-gen zh_CN.UTF-8 && \
    locale-gen en_US.UTF-8 && \
    update-locale LANG=zh_CN.UTF-8

ENV LANG=zh_CN.UTF-8
ENV LC_ALL=zh_CN.UTF-8

# openvino for arm64
RUN \
    echo "Building OpenVINO from source for arm64..." && \
    git clone -b 2025.4.0 https://gitee.com/openvinotoolkit-prc/openvino.git /tmp/openvino && \
    cd /tmp/openvino && \
    chmod +x scripts/submodule_update_with_gitee.sh && \
    ./scripts/submodule_update_with_gitee.sh && \
    ./install_build_dependencies.sh && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/openvino ;

# qd_2026_dart
RUN \
    apt-get update && apt-get install -y \
    libeigen3-dev libceres-dev libopencv-dev libfmt-dev \
    libspdlog-dev libyaml-cpp-dev nlohmann-json3-dev libusb-1.0-0-dev libgtk-3-dev \
    libcanberra-gtk-module libcanberra-gtk3-module x11-apps \
    ffmpeg \
    && apt clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# 添加环境变量 到 bashrc
RUN echo "source /usr/local/setupvars.sh" >> /root/.bashrc

# 启动环境变量
RUN cat <<EOF > /rm_entrypoint.sh
#!/bin/bash

if [ -f "/usr/local/setupvars.sh" ]; then
    source "/usr/local/setupvars.sh" || true
fi

exec "\$@"
EOF
RUN chmod +x /rm_entrypoint.sh
ENTRYPOINT ["/rm_entrypoint.sh"]
CMD ["/bin/bash"]