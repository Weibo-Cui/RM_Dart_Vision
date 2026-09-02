#!/bin/bash
# watch_dog.sh

WORKING_DIR="/dart_ws/"
EXEC_PATH="./build/standard"
CONFIG_FILE="$WORKING_DIR/configs/test.yaml"
TIMEOUT=8

# 日志输出目录
SESSION_TIME=$(date +'%Y-%m-%d_%H-%M-%S')
LOG_DIR="$WORKING_DIR/watchdog/$SESSION_TIME"
mkdir -p "$LOG_DIR"

function log() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_DIR/watchdog.log"
}

function bringup() {
    LAUNCH_TIME=$(date +'%Y-%m-%d_%H-%M-%S')
    DART_LOG_FILE="$LOG_DIR/dart_$LAUNCH_TIME.log"
    log "Starting Dart Vision, logging to $DART_LOG_FILE..."
    source /usr/local/setupvars.sh
    nohup $EXEC_PATH > "$DART_LOG_FILE" 2>&1 &
}

function restart() {
    log "Performing cleanup of Dart Vision processes..."
    pkill -9 -f "standard" || true
    sleep 1
    bringup
}

# 更新环境变量
source /usr/local/setupvars.sh

restart
sleep $TIMEOUT

# 看门狗监测
while true; do
    if ! pgrep -f "standard" > /dev/null; then
        log "    Program exited! Restarting in $TIMEOUT second..."
        restart
    fi
    sleep $TIMEOUT
done