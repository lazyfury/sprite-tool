# rembg-api — 独立背景清理服务（最小实现）

基于 [rembg](https://github.com/danielgatis/rembg)（AI 抠图，U2-Net 系列模型）的独立 HTTP 服务。
供 sprite-split 通过 `--bg-backend remote --bg-url <URL>` 调用，也可被任何 HTTP 客户端直接使用。

## 快速启动

```bash
cd examples/rembg-api
./run.sh                 # 首次：建 venv + 装依赖 + 下载模型（模型 ~176MB，首次请求时下载）
# 或手动：
#   python3 -m venv .venv && source .venv/bin/activate
#   pip install -r requirements.txt
#   uvicorn app.main:app --host 0.0.0.0 --port 8000
```

> 首次调用 `/api/remove-background` 时会从 GitHub 下载模型（`isnet-general-use`，约 176MB），
> 之后缓存于 `~/.u2net/`。可用 `REMBG_MODEL` 换更小的模型（如 `u2netp`，约 4.7MB）。

## 配置（环境变量，见 `.env.example`）

| 变量 | 默认 | 说明 |
|---|---|---|
| `REMBG_API_HOST` | `0.0.0.0` | 监听地址 |
| `REMBG_API_PORT` | `8000` | 端口 |
| `REMBG_MODEL` | `isnet-general-use` | rembg 模型名 |
| `REMBG_MAX_UPLOAD_MB` | `20` | 上传大小上限 |

## URL 格式

| 方法 | URL | 请求 | 响应 |
|---|---|---|---|
| GET | `/healthz` | — | `{"status":"ok","model":"..."}` |
| POST | `/api/remove-background` | multipart 文件上传（字段名 **`image`**）+ 可选 form 参数 | `image/png` 透明 PNG |
| POST | `/api/remove-background/url` | JSON `{"url":"https://...","alpha_matting":false}` | `image/png` 透明 PNG |

可选参数（两个 POST 接口通用）：
- `alpha_matting: bool` — 边缘精修（默认 false；首次启用会额外下载模型）
- `post_process_mask: bool` — 边缘后处理（默认 false）
- `only_mask: bool` — 返回黑白 mask 而非透明图（默认 false）

错误约定：非图片 / 抓取失败 / 处理失败 → 4xx/5xx + `{"error": "..."}`。

## curl 示例

```bash
# 上传文件 → 透明 PNG
curl -sS http://127.0.0.1:8000/api/remove-background \
  -F "image=@tests/fixtures/grid8_sheet.png" -o out.png

# 指定参数
curl -sS http://127.0.0.1:8000/api/remove-background \
  -F "image=@input.png" -F "alpha_matting=true" -o out.png

# 远程图片 URL → 透明 PNG
curl -sS http://127.0.0.1:8000/api/remove-background/url \
  -H "Content-Type: application/json" \
  -d '{"url":"https://example.com/a.png"}' -o out.png

# 健康检查
curl -sS http://127.0.0.1:8000/healthz
```

## 与 sprite-split 集成

```bash
# 1. 启动本服务（端口 8000）
./examples/rembg-api/run.sh

# 2. C++ CLI 通过 URL 调用（其他子命令 split/manual/from-json/sheet/info 同样支持）
build/sprite-split split char.png --remove-background --bg-backend remote \
  --bg-url http://127.0.0.1:8000 --output out/sprites --json
```

服务不可达时 C++ 侧会 `warning:` 并自动回退纯算法后端（四角采样 + flood fill），不影响可用性。
