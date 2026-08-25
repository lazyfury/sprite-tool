@tool
extends HSeparator
## 注册表条目分隔线（可复用场景）：样式化 HSeparator。
## 颜色在 reg_sep.tscn 的 theme_override_styles/separator 里改（StyleBoxLine，用户可改）；
## 代码需要时可用 set_color() 运行时覆盖（例如跟随编辑器主题）。
## 注意：HSeparator 的 separator 主题项是 StyleBox（绘制走 stylebox），
## 设色要用 StyleBoxLine，theme_override_colors/separator 只是旧 fallback。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

func set_color(c: Color) -> void:
	var sb: StyleBoxLine = StyleBoxLine.new()
	sb.color = c
	add_theme_stylebox_override("separator", sb)
