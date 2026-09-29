# 基于同济大学sp_vision框架镖架制导

## 实现内容
- [x] opencv稳定识别绿灯并发送像素差
- [x] 可用神经网络稳定识别绿灯
- [x] 在网页端显示可视化内容
- [x] 在网页端动态调试opencv参数
- [x] 录制相机原始图像
- [x] 可对录制好的视频进行实时识别，动态调参
- [x] 加入跟踪器，避免其它物体干扰

## 方案一：在ubuntu原生环境下部署项目
## 环境配置
理论上sp_vision能跑这个就能跑
##  编译
```bash
cmake -B build
make -C build/ -j 4   
```
## 运行
```bash
./build/standard
```

## 方案二：使用docker快速部署项目
- step1 拉取做好的镜像
```bash
docker pull woberr/qidian:dart_arm64v8_2
```

- step2 构建容器
```bash
docker compose up -d
```

- step3 进入容器终端
```bash
docker exec -it rv_dart_ bash
```

- step4 在容器环境内编译运行（此部分已写，略）

## docker自启动
- step1 在`docker-compose.yaml`里改成：
```yaml
    # 启动命令 (对应最后那一串)
    # 进终端
    # command: -c "sleep infinity"
    # 自启动
    command: -c "source /dart_ws/dart_watch_dog.sh" 
```
- step2 构建容器（此部分已写，略）

- 自启动运行时，若要让已更改的`test.yaml`参数重新生效，需在终端执行
```bash
pkill -9 -f "standard"
```

## 网页端窗口显示视频流
> 向网页端推送视频流解决了传统的`X11`转发环境依赖高、画面卡顿、自启动时主程序崩溃问题，是特别实用的功能
- 程序运行时打开浏览器，地址栏输入`http://localhost:8080`，就可在此链接中查看推送的视频流，如果使用ssh连接目标主机后在该主机当中跑程序，把`localhost`替换成目标主机的ip，比如`http://192.168.137.167:8080`
- 若在`configs/test.yaml`中`debug: true`，`use_trackbar: false`，网页端只显示识别可视化图像
    ![1.png](/docs/1.png)
- 若在`configs/test.yaml`中`debug: true`，`use_trackbar: true`，网页端显示识别可视化图像、二值化图像、opencv参数滑条  
    ![2.png](/docs/2.png)
- 使用自启动运行程序时，可在网页端查看可视化结果，也可动态调opencv识别参数

## 开发实用技巧

### 解决nomachine工具远程连接窗口显示黑屏情况
> 目前最好的方法：关闭物理显示器的图形界面，给nomachine虚拟桌面一个空间，但这样做会无法使用外接显示器
- step 1: 设置系统默认启动到命令行模式
```bash
sudo systemctl set-default multi-user.target
```
- step 2: 重启系统
```bash
sudo reboot now
```
- 若想恢复原来的情况，输入```sudo systemctl set-default graphical.target```，然后再重启

### 使用rsync同步文件

```bash
rsync -rlptzvP --delete \
--exclude='build/' \
--exclude='logs/' \
--exclude='MvSdkLog/' \
--exclude='records/' \
--exclude='watchdog/' \
--exclude='videos' \
./ qidian@192.168.137.x:~/qd_2026_dart/
```
### commit 规范

- 提交信息应包含：类型 (Type)、描述 (Subject) 以及可选的 范围（scope）、正文 (Body) 和 脚注 (Footer)

```
<type>(<scope>): <subject>

<body>

<footer>
```

- 常用类型 (Type)

    | 类型 | 描述 |
    | :--- | :--- | 
    | **feat** | 新增功能 (feature) |
    | **fix** | 修复 Bug |
    | **docs** | 文档修改 (Documentation) | 
    | **style** | 代码格式修改 (不影响逻辑，如空格、分号等) | 
    | **refactor** | 代码重构 (既不是修复 Bug 也不是新增功能) | 
    | **perf** | 性能优化 (Performance) | 
    | **test** | 增加或修改测试用例 | 
    | **chore** | 构建过程、辅助工具或依赖库的变动 | 