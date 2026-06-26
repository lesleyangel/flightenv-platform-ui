# Codex WebUI 启动与验收备忘

本文记录 Codex 在本机验证 WebUI 时使用的固定入口，避免后续重复试错。

## 固定启动入口

首选用户可见入口：

```powershell
F:\code\FlightEnvMultiRepo\flightenv-controller-ui\webui\start_web_ui.cmd
```

该脚本会：

- 将端口固定为 `8787`；
- 切到 `flightenv-controller-ui\webui`；
- 启动 `python server.py`；
- 打开 `http://127.0.0.1:8787/?page=workspace`。

Codex 自动验收时，如果需要让服务在后台常驻，可使用等价命令：

```powershell
cd /d F:\code\FlightEnvMultiRepo\flightenv-controller-ui\webui
python server.py
```

健康检查：

```powershell
Invoke-RestMethod http://127.0.0.1:8787/api/health
```

期望返回：

```json
{"ok":true}
```

## 当前三分支验收对象草稿

对象包：

```text
F:\code\FlightEnvMultiRepo\flightenv-object-reentry-vehicle
```

对象草稿：

```text
reentry_vehicle_ui_full_online_3s_three_branch_20260622
```

草稿包路径：

```text
F:\code\FlightEnvMultiRepo\_local_artifacts\webui-object-drafts\reentry_vehicle\reentry_vehicle_ui_full_online_3s_three_branch_20260622\package
```

关键配置：

- 在线 workflow：`reentry.ui_full_online_3s_three_branch.v1`
- 预测 workflow：`reentry.ui_future_prediction_3s.v1`
- run profile：`ui_full_online_3s_three_branch`
- 输入 fixture：`fixtures\sensor_stream_db70_3s.json`
- 主时间步：`3 s/frame`
- 预测分支数：`3`
- 预测分支触发帧：`19, 39, 59`

## 已通过的验收 run

```text
F:\code\FlightEnvMultiRepo\_local_artifacts\platform-runtime\mainline-runs\webui_acceptance_3s_three_branch_verified_20260622
```

验收结果：

- 在线主线：`70/70` 帧；
- 主时钟：`0 -> 207 s`，每帧 `3 s`；
- 预测分支：`3/3` 完成；
- 分支 id：
  - `predict.frame_0019.000`
  - `predict.frame_0039.001`
  - `predict.frame_0059.002`
- 每条预测分支：`20` 个 branch step；
- 每条预测分支：`150` 个 field artifact 引用；
- 每条预测分支：`30` 个 QoI 引用。

## UI 页面验收路径

启动服务后按下面页面检查：

```text
http://127.0.0.1:8787/?page=workspace
http://127.0.0.1:8787/?page=workflow
http://127.0.0.1:8787/?page=runconfig
http://127.0.0.1:8787/?page=online
http://127.0.0.1:8787/?page=runtime
http://127.0.0.1:8787/?page=evidence
```

历史 run 回放可直接打开：

```text
http://127.0.0.1:8787/?page=inspector&run=platform-runtime/mainline-runs/webui_acceptance_3s_three_branch_verified_20260622
http://127.0.0.1:8787/?page=evidence&run=platform-runtime/mainline-runs/webui_acceptance_3s_three_branch_verified_20260622
```

## 真实操作顺序

1. 打开工作空间页。
2. 载入对象包 `flightenv-object-reentry-vehicle`。
3. 激活对象草稿 `reentry_vehicle_ui_full_online_3s_three_branch_20260622`。
4. 进入工作流编排页，选择 `reentry.ui_full_online_3s_three_branch.v1`。
5. 进入运行配置页，选择 `ui_full_online_3s_three_branch`，确认 `3 s/frame` 与 `3` 个预测分支。
6. 在工作流页执行 `validate -> compile -> preflight`。
7. 在在线运行页执行 `初始化对象 / 加载模型`。
8. 执行 `启动运行`。
9. 在在线运行页观察在线主分支、三个预测分支、时间线、field artifact 与 QoI。
10. 在证据回放页确认 run package、branch registry、runtime events、data-plane artifacts。

## Codex 浏览器说明

当前 Codex 内置浏览器可能拦截本机 `localhost/127.0.0.1` 页面。遇到这种情况时，不判断为 WebUI 启动失败；以 `start_web_ui.cmd` 打开的普通浏览器窗口为准。
