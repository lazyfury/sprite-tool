@tool
class_name SpriteSplitterData
extends Resource

## Sprite Splitter 项目数据（.tres，可运行时编辑并保存）：
## - 兼容 meta.json 数据（sprites = rects 数组，与导出 meta.json 同源）
## - 保存常用切分参数 + 导出位置 + 项目名称
## - 通过素材 uid 关联（sheet_uid），加载素材时自动匹配加载/初始化
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

@export var sheet_uid: String = ""          # 关联素材 uid（uid://...，外部素材为空）
@export var source_image: String = ""       # 素材 res:// 路径
@export var project_name: String = ""       # 项目名称（默认=素材名，可编辑）
@export var mode: String = "auto"
@export var min_width: int = 2
@export var min_height: int = 2
@export var grid_cell_size: int = 16
@export var merge_distance: int = 0
@export var alpha_threshold: int = 1
@export var background_threshold: int = 12
@export var out_dir: String = "res://out_sprites/ui"
@export var export_mode: int = 0
@export var sprites: Array[Rect2i] = []     # 切分结果（meta.json sprites 兼容）
@export var modified_at: int = 0            # 最后修改时间（Unix 秒，保存时更新）
