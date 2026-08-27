#!/usr/bin/env bash
# =============================================================================
# pack_release.sh — sprite-tool 发布打包（macOS arm64）
#
# 前置：产物必须已构建
#   CLI:              cmake -B build && cmake --build build -j
#   GDExtension:      cmake -S godot -B godot/build      -G Ninja -DCMAKE_BUILD_TYPE=Debug   -DGODOTCPP_TARGET=template_debug \
#                       -DGODOTCPP_DISABLE_EXCEPTIONS=OFF -DGODOTCPP_USE_STATIC_CPP=ON
#                     cmake -S godot -B godot/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_release \
#                       -DGODOTCPP_DISABLE_EXCEPTIONS=OFF -DGODOTCPP_USE_STATIC_CPP=ON -DGODOTCPP_USE_HOT_RELOAD=OFF
#
# 产出：release/
#   sprite-split-<CLI_VER>-macos-arm64.zip   CLI 二进制包（sprite-split + README）
#   sprite-tool-plugin-<VER>.zip            Godot 插件包（addons/sprite_tool/，含 debug+release 动态库）
#   sprite-tool-demo-<VER>.zip              Demo 工程包（干净化：无 .godot 缓存 / 生成产物 / 测试数据）
#   SHA256SUMS
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

OUT="release"
STAGE="$(mktemp -d /tmp/sps_pack.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT

PLUGIN_DIR="godot/project/addons/sprite_tool"
GDEXT="bin/sps_gdextension.gdextension"
DBG_LIB="bin/macos/sps_gdextension.macos.template_debug.arm64.dylib"
REL_LIB="bin/macos/sps_gdextension.macos.template_release.arm64.dylib"

# ---- 版本 ----
VER="$(sed -n 's/^version="\(.*\)"/\1/p' "$PLUGIN_DIR/plugin.cfg")"
[ -n "$VER" ] || { echo "ERROR: 无法从 plugin.cfg 读取版本" >&2; exit 1; }
CLI_VER="$("$ROOT/build/sprite-split" --version | awk '{print $2}')"

# ---- 产物校验 ----
echo "==> 校验产物"
[ -x build/sprite-split ] || { echo "ERROR: 缺 CLI 二进制 build/sprite-split（先 cmake --build build）" >&2; exit 1; }
[ -f "$PLUGIN_DIR/$GDEXT" ]       || { echo "ERROR: 缺 $PLUGIN_DIR/$GDEXT" >&2; exit 1; }
[ -f "$PLUGIN_DIR/$DBG_LIB" ]     || { echo "ERROR: 缺 debug 动态库（先构建 godot/build）" >&2; exit 1; }
[ -f "$PLUGIN_DIR/$REL_LIB" ]     || { echo "ERROR: 缺 release 动态库（先构建 godot/build-release）" >&2; exit 1; }
for f in plugin.cfg editor_plugin.gd icon.svg; do
  [ -f "$PLUGIN_DIR/$f" ] || { echo "ERROR: 缺 $PLUGIN_DIR/$f" >&2; exit 1; }
done
echo "    CLI v$CLI_VER / 插件 v$VER / debug+release 动态库 OK"

mkdir -p "$OUT"
rm -f "$OUT"/sprite-split-* "$OUT"/sprite-tool-* "$OUT"/SHA256SUMS

# =============================================================================
# 1) CLI 二进制包
# =============================================================================
echo "==> 打包 CLI"
CLI_DIR="$STAGE/cli/sprite-split-$CLI_VER-macos-arm64"
mkdir -p "$CLI_DIR"
install -m 755 build/sprite-split "$CLI_DIR/sprite-split"
cp README.md "$CLI_DIR/README.md"
(cd "$STAGE/cli" && zip -X -r -q "$ROOT/$OUT/sprite-split-$CLI_VER-macos-arm64.zip" "sprite-split-$CLI_VER-macos-arm64")

# =============================================================================
# 2) 插件包（addons/sprite_tool/，直接可拷入 res://addons/）
# =============================================================================
echo "==> 打包插件"
PKG_DIR="$STAGE/pkg"
mkdir -p "$PKG_DIR"
# 复制全部插件文件，排除 *.import（导入元数据，编辑器自动生成）
# 保留 .uid（Godot 4.4+ 资源 UID 映射，防止引用错乱）
find "$PLUGIN_DIR" -type f ! -name "*.import" | while read -r f; do
  rel="${f#"$PLUGIN_DIR"/}"
  mkdir -p "$PKG_DIR/addons/sprite_tool/$(dirname "$rel")"
  cp -p "$f" "$PKG_DIR/addons/sprite_tool/$rel"
done
# 动态库必须带可执行位
chmod 755 "$PKG_DIR/addons/sprite_tool/$DBG_LIB" "$PKG_DIR/addons/sprite_tool/$REL_LIB"
(cd "$PKG_DIR" && zip -X -r -q "$ROOT/$OUT/sprite-tool-plugin-$VER.zip" addons)

# =============================================================================
# 3) Demo 工程包（干净化：无缓存 / 生成产物 / 测试数据 / .import）
# =============================================================================
echo "==> 打包 demo"
DEMO_DIR="$STAGE/demo/sprite-tool-demo"
mkdir -p "$DEMO_DIR"
PROJ="godot/project"
# 白名单式拷贝：显式列出的条目 + addons + sprites 素材
for item in project.godot main.gd main.gd.uid main.tscn addons sprites; do
  if [ -e "$PROJ/$item" ]; then
    cp -R "$PROJ/$item" "$DEMO_DIR/"
  fi
done
# 剔除 addons 导入元数据与测试 harness 之外的杂项
find "$DEMO_DIR" -name "*.import" -delete
find "$DEMO_DIR" -name ".DS_Store" -delete
# 动态库可执行位
chmod 755 "$DEMO_DIR"/addons/sprite_tool/bin/macos/*.dylib 2>/dev/null || true
# main.tscn 最小化：剥离指向生成产物（res://out_sprites/...）的 Sprite2D 纹理引用，
# 只保留 root + main.gd 脚本（冒烟测试入口）。场景/脚本 uid 保持不变。
cat > "$DEMO_DIR/main.tscn" <<'TSCN'
[gd_scene format=3 uid="uid://cclkq32g1wpin"]

[ext_resource type="Script" uid="uid://cbprljxdy0f4s" path="res://main.gd" id="1_main"]

[node name="Main" type="Node"]
script = ExtResource("1_main")
TSCN
# 生成清单
(cd "$DEMO_DIR" && find . -type f | sort > MANIFEST.txt)
(cd "$STAGE/demo" && zip -X -r -q "$ROOT/$OUT/sprite-tool-demo-$VER.zip" sprite-tool-demo)

# =============================================================================
# 4) 校验 + 校验和
# =============================================================================
echo "==> 校验"
for z in "$OUT"/sprite-split-* "$OUT"/sprite-tool-plugin-* "$OUT"/sprite-tool-demo-*; do
  unzip -t -q "$z" || { echo "ERROR: $z 损坏" >&2; exit 1; }
  echo "    $(basename "$z")  $(du -h "$z" | cut -f1)  ($(unzip -l "$z" | tail -1 | awk '{print $2}') files)"
done
(cd "$OUT" && shasum -a 256 *.zip > SHA256SUMS)
echo
echo "==> 完成：$OUT/"
ls -lh "$OUT"/*.zip "$OUT"/SHA256SUMS
