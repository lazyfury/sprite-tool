#!/usr/bin/env bash
# rembg-api 一键启动：自动建 venv / 装依赖 / 启动
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -d .venv ]; then
  echo "[rembg-api] creating venv..."
  python3 -m venv .venv
fi
# shellcheck disable=SC1091
source .venv/bin/activate

if ! python -c "import fastapi, rembg" >/dev/null 2>&1; then
  echo "[rembg-api] installing dependencies (first run: ~300MB disk)..."
  pip install -r requirements.txt
fi

exec uvicorn app.main:app --host "${REMBG_API_HOST:-0.0.0.0}" --port "${REMBG_API_PORT:-8000}"
