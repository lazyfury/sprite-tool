"""rembg-api — 独立背景清理服务（最小实现）。

URL 约定：
  GET  /healthz                        → {"status":"ok","model":"..."}
  POST /api/remove-background          上传图片（multipart，字段名 image）→ 透明 PNG
  POST /api/remove-background/url      JSON {"url": "https://..."} → 透明 PNG

可选参数（form 或 JSON 字段，均为可选）：
  alpha_matting: bool   边缘 alpha matting 精修（首次启用会额外下载模型）
  post_process_mask: bool  边缘后处理
  only_mask: bool       返回黑白 mask 而非透明图

错误：非图片/抓取失败/处理失败 → 4xx/5xx + JSON {"error": "..."}
"""

import io

import requests
from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.responses import Response

from .config import config
from . import remover

app = FastAPI(title="rembg-api", version="0.1.0")


def _png_response(png_bytes: bytes) -> Response:
    return Response(content=png_bytes, media_type="image/png")


def _validate_image(data: bytes, origin: str) -> None:
    """用 PIL 校验可解码；失败抛 HTTPException(400)。"""
    if len(data) > config.max_upload_bytes:
        raise HTTPException(
            status_code=413,
            detail=f"{origin}: file too large (max {config.max_upload_bytes // (1024 * 1024)} MB)",
        )
    try:
        from PIL import Image

        img = Image.open(io.BytesIO(data))
        img.verify()
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"{origin}: not a decodable image: {e}")


def _run_remover(
    data: bytes,
    alpha_matting: bool,
    post_process_mask: bool,
    only_mask: bool,
) -> Response:
    try:
        png = remover.remove(
            data,
            alpha_matting=alpha_matting,
            post_process_mask=post_process_mask,
            only_mask=only_mask,
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"background removal failed: {e}")
    return _png_response(png)


@app.get("/healthz")
def healthz() -> dict:
    return {"status": "ok", "model": config.model}


@app.post("/api/remove-background")
async def remove_background_upload(
    image: UploadFile = File(..., description="image file (png/jpg/webp/...)"),
    alpha_matting: bool = Form(False),
    post_process_mask: bool = Form(False),
    only_mask: bool = Form(False),
) -> Response:
    data = await image.read()
    _validate_image(data, "upload")
    return _run_remover(data, alpha_matting, post_process_mask, only_mask)


class UrlBody:
    """POST /api/remove-background/url 的 JSON 格式：{"url": str, 可选参数同 upload}"""


@app.post("/api/remove-background/url")
async def remove_background_url(body: dict) -> Response:
    url = body.get("url")
    if not isinstance(url, str) or not url.strip():
        raise HTTPException(status_code=400, detail="missing 'url' string field")
    try:
        resp = requests.get(url, timeout=30)
    except requests.RequestException as e:
        raise HTTPException(status_code=502, detail=f"failed to fetch url: {e}")
    if resp.status_code != 200:
        raise HTTPException(
            status_code=502,
            detail=f"url fetch failed: HTTP {resp.status_code}",
        )
    _validate_image(resp.content, f"url {url}")
    return _run_remover(
        resp.content,
        alpha_matting=bool(body.get("alpha_matting", False)),
        post_process_mask=bool(body.get("post_process_mask", False)),
        only_mask=bool(body.get("only_mask", False)),
    )
