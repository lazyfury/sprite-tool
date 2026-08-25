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
@export var source_texture: Texture2D = null   # 源素材纹理引用（项目内 res:// 素材可序列化；外部素材/内存图为 null，缩略图走路径回退）
@export var project_name: String = ""       # 项目名称（默认=素材名，可编辑）
@export var mode: String = "auto"
@export var min_width: int = 2
@export var min_height: int = 2
@export var grid_cell_size: int = 16
@export var merge_distance: int = 0
@export var alpha_threshold: int = 1
@export var slice_policy: String = "auto"   # auto 模式切割策略：auto | components | grid
@export var padding: int = 0                # auto 模式 rect 外扩
@export var background_threshold: int = 12
@export var background_backend: String = "color"   # 去背景后端：color | remote
@export var use_bg_color: bool = false             # 手动吸色开关（color 后端）
@export var bg_color: Color = Color(1, 1, 1, 1)    # 手动背景色
@export var bg_url: String = "http://127.0.0.1:8000"  # remote 后端 base URL
@export var out_dir: String = "res://out_sprites/ui"
@export var export_mode: int = 0
@export var sprites: Array = []   # 切分结果（复杂结构 Array[Dictionary]；兼容旧 Array[Rect2i]，读取时转换）
@export var modified_at: int = 0            # 最后修改时间（Unix 秒，保存时更新）
