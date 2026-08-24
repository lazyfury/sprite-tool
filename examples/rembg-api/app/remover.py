"""rembg 最小封装：懒加载模型，remove() 返回 PNG bytes。"""

import io
from functools import lru_cache

import rembg

from .config import config


@lru_cache(maxsize=1)
def _get_session(model_name: str) -> rembg.sessions.BaseSession:
    """按模型名创建 rembg 推理会话（模型首次使用时下载）。"""
    return rembg.new_session(model_name)


def remove(
    image_bytes: bytes,
    alpha_matting: bool = False,
    post_process_mask: bool = False,
    only_mask: bool = False,
) -> bytes:
    """去除背景，返回 PNG bytes。输入/输出均为内存字节，不落盘。"""
    session = _get_session(config.model)
    result = rembg.remove(
        image_bytes,
        session=session,
        alpha_matting=alpha_matting,
        post_process_mask=post_process_mask,
        only_mask=only_mask,
        alpha_matting_foreground_threshold=260,
        alpha_matting_background_threshold=5,
        alpha_matting_erode_size=5,
    )
    # rembg 对 bytes 输入直接返回 bytes（PNG 编码）；PIL.Image 输入返回 PIL.Image
    if isinstance(result, (bytes, bytearray)):
        return bytes(result)
    buf = io.BytesIO()
    result.save(buf, format="PNG")
    return buf.getvalue()
