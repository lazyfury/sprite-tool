"""rembg-api 配置：全部来自环境变量，无外部配置依赖。"""

import os


class Config:
    host: str
    port: int
    model: str
    max_upload_bytes: int

    def __init__(self) -> None:
        self.host = os.getenv("REMBG_API_HOST", "0.0.0.0")
        self.port = int(os.getenv("REMBG_API_PORT", "8000"))
        self.model = os.getenv("REMBG_MODEL", "isnet-general-use")
        max_mb = float(os.getenv("REMBG_MAX_UPLOAD_MB", "20"))
        self.max_upload_bytes = int(max_mb * 1024 * 1024)


config = Config()
