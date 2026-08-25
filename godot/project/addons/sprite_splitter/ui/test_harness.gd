@tool
extends Control

## 测试 harness（M5.3）：主视图 + 侧栏同屏实例化，注入同一 SpsController，
## 验证跨区域交互（dock 按钮 → 主画布响应）。SPS_UI_TEST=1 跑全链路断言后退出。

const DEFAULT_SHEET: String = "res://sprites/sheet.png"
const AUTO_TEST_FLAG: String = "--sps-ui-test"
const AUTO_PROBE_FLAG: String = "--sps-ui-probe"
const PROBE_OUT: String = "res://out_probe/ui_probe.json"
const UiProbeScript: GDScript = preload("res://addons/sprite_splitter/ui/ui_probe.gd")

var _controller: SpsController = null
var _main: Control = null
var _side: Control = null
var _fail: bool = false

@onready var _main_holder: Control = get_node("MainHolder")
@onready var _side_holder: Control = get_node("SideHolder")


# 清理测试目录残留的 .tres（独立测试目录 res://sps_data_test，不碰用户数据）
func _clean_test_data(test_dir: String) -> void:
	var dir: DirAccess = DirAccess.open(test_dir)
	if dir == null:
		return
	dir.list_dir_begin()
	var f: String = dir.get_next()
	while not f.is_empty():
		if f.ends_with(".tres"):
			DirAccess.remove_absolute(test_dir + "/" + f)
		f = dir.get_next()
	dir.list_dir_end()


func _ready() -> void:
	# 测试数据隔离：独立目录 res://sps_data_test（构造注入），绝不碰用户真实 sps_data/
	_clean_test_data("res://sps_data_test")
	_controller = SpsController.new(get_tree(), "res://sps_data_test")
	_main = load("res://addons/sprite_splitter/ui/sprite_splitter_main.tscn").instantiate()
	_side = load("res://addons/sprite_splitter/ui/sprite_splitter_side.tscn").instantiate()
	_main_holder.add_child(_main)
	_side_holder.add_child(_side)
	_main.set_controller(_controller)
	_side.set_controller(_controller)
	_main.set_side(_side)   # 主视图需侧栏参数（注册表切换前保存）
	_selftest()
	if _auto_test_requested():
		_auto_test()
	if _auto_probe_requested():
		_auto_probe()


func _check(cond: bool, msg: String) -> void:
	if cond:
		print("[sps-ui] PASS: ", msg)
	else:
		printerr("[sps-ui] FAIL: ", msg)
		_fail = true


func _auto_test_requested() -> bool:
	if OS.get_environment("SPS_UI_TEST") == "1":
		return true
	return OS.get_cmdline_user_args().has(AUTO_TEST_FLAG)


func _auto_probe_requested() -> bool:
	if OS.get_environment("SPS_UI_PROBE") == "1":
		return true
	return OS.get_cmdline_user_args().has(AUTO_PROBE_FLAG)


# 插桩：加载素材 + 切分（列表/注册表有真实数据）→ 遍历主视图+侧栏全部 Control，
# 记录位置信息 + 主题影响数据，写 JSON 后退出。SPS_UI_PROBE=1 或 --sps-ui-probe。
func _auto_probe() -> void:
	print("[sps-ui] === ui probe ===")
	# headless 默认窗口极小（64x64）→ 手动撑大，否则布局全塌（位置数据失真）
	get_window().size = Vector2i(1280, 800)
	_controller.load_image(DEFAULT_SHEET)
	_side._on_split()
	for _i: int in 8:   # 等布局稳定（列表/缩略图/样式生效）
		await get_tree().process_frame
	var report: Dictionary = UiProbeScript.write_report(self, PROBE_OUT)
	print("[sps-ui] probe done: controls=", report.get("count", 0),
			" out=", PROBE_OUT)
	get_tree().quit(0)


func _selftest() -> void:
	var rects: Array = _controller.splitter.split(Image.load_from_file(DEFAULT_SHEET),
			{"mode": "auto", "min_width": 2, "min_height": 2})
	print("[sps-ui] selftest: split -> ", rects.size(), " rects (expect 64)")
	_check(rects.size() == 64, "selftest: split 64 rects")


func _auto_test() -> void:
	print("[sps-ui] === auto test ===")
	var canvas: Control = _main.get_canvas_view()
	var out_dir: String = "res://out_sprites/ui_test"

	# 加载 → 切分（自动分析）→ 画布更新
	_check(_controller.load_image(DEFAULT_SHEET), "auto test: load image")
	var expect_dir: String = "res://out_sprites/ui/" \
			+ _controller.data.sheet_uid.trim_prefix("uid://")
	_check(_controller.data.out_dir == expect_dir,
			"auto test: default out_dir has uid subdir")
	_side._on_split()
	_check(not _controller.rects.is_empty(), "auto test: split produced rects")
	_check(canvas.get("_rects").size() == _controller.rects.size(),
			"auto test: canvas rects synced")
	# 切分数据列表（主视图右侧面板）
	var split_list: ItemList = _main.get_node(
			"MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/ListArea/SplitList")
	var split_empty: Label = _main.get_node(
			"MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/ListArea/EmptyLabel")
	# 切片数据卡片：标题 + 空状态
	_check(_main.get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/TitleLabel").text
			== "切片数据", "auto test: split card title")
	_check(_main.get("_split_card") != null, "auto test: split card panel themed")
	_check(split_list.item_count == _controller.rects.size(),
			"auto test: split list shows all rects")
	# 新 auto 决策：COMPONENTS_IN_GRID → 首个 rect 是组件 bbox (4,4,8,8)，不是 16x16 cell
	_check(split_list.get_item_text(0).begins_with("#1  (4,4)"),
			"auto test: split list first entry format (bbox 4,4)")
	_check(not split_empty.visible, "auto test: empty hint hidden when rects exist")

	# 导出三模式
	_controller.export(0, out_dir, _side._build_options())
	await get_tree().process_frame
	_check(FileAccess.file_exists(out_dir + "/sprite_01.png"), "auto test: PNG exported")
	_controller.export(1, out_dir, _side._build_options())
	await get_tree().process_frame
	var j: Variant = JSON.parse_string(FileAccess.get_file_as_string(out_dir + "/meta.json"))
	_check(j is Dictionary and int(j.get("sprites", []).size()) == _controller.rects.size(),
			"auto test: meta.json count matches")
	_controller.export(2, out_dir, _side._build_options())
	await get_tree().process_frame
	_check(FileAccess.file_exists(out_dir + "/atlas_01.tres"), "auto test: atlas tres exported")

	# 导入 meta.json → 画布更新
	_controller.import_meta(out_dir + "/meta.json")
	_check(_controller.rects.size() == 64, "auto test: import meta rects == 64")
	_check(_controller.rects[0] == Rect2i(0, 0, 16, 16), "auto test: first rect matches")

	# 画布相机式变换
	_check(canvas.has_image(), "auto test: canvas has image")
	_check(canvas.clip_contents, "auto test: canvas clips contents")
	var scr: Vector2 = canvas.world_to_screen(Vector2(0, 0))
	_check(canvas.screen_to_world(scr).distance_to(Vector2.ZERO) < 0.01,
			"auto test: world->screen->world roundtrip")
	var p_before: Vector2 = canvas.world_to_screen(Vector2(16, 16))
	canvas.zoom_at(canvas.size * 0.5, 2.0)
	var p_after: Vector2 = canvas.world_to_screen(Vector2(16, 16))
	_check(p_before.distance_to(p_after) > 0.01, "auto test: zoom changes screen pos")
	_check(canvas.screen_to_world(p_after).distance_to(Vector2(16, 16)) < 0.01,
			"auto test: world coord invariant under zoom")
	# 触控板手势：双指捏合缩放（双指拖动已按用户要求移除）
	var zoom0: float = canvas.get_zoom()
	var mg: InputEventMagnifyGesture = InputEventMagnifyGesture.new()
	mg.position = canvas.size * 0.5
	mg.factor = 2.0
	canvas._gui_input(mg)
	_check(absf(canvas.get_zoom() - zoom0 * 2.0) < 0.001,
			"auto test: magnify gesture zooms")

	# 工具模式（MOVE/SELECT/CROP）
	_check(canvas.get_tool() == canvas.Tool.SELECT, "auto test: default tool SELECT")
	canvas.set_tool(canvas.Tool.MOVE)
	_check(canvas.get_tool() == canvas.Tool.MOVE, "auto test: tool switch MOVE")
	canvas.set_tool(canvas.Tool.SELECT)
	var hit: Rect2i = canvas.pick_rect_at(Vector2(8, 8))
	_check(hit == Rect2i(0, 0, 16, 16), "auto test: pick (8,8) -> first cell")
	canvas.set("_drag_start", canvas.world_to_screen(Vector2(0, 0)))
	canvas.set("_drag_cur", canvas.world_to_screen(Vector2(32, 32)))
	canvas._on_drag_select()
	_check(canvas.get_selected_rects().size() >= 4, "auto test: drag select >=4 cells")
	canvas.set_tool(canvas.Tool.CROP)
	canvas.set("_drag_start", canvas.world_to_screen(Vector2(0, 0)))
	canvas.set("_drag_cur", canvas.world_to_screen(Vector2(16, 16)))
	canvas._finish_crop()
	_check(canvas.get("_selection") == Rect2i(0, 0, 16, 16), "auto test: crop rect matches")

	# 去背景（独立按钮能力）
	var bg_img: Image = _controller.splitter.remove_background(_controller.image,
			{"background_threshold": 12})
	_check(bg_img != null and bg_img.get_pixel(0, 0).a < 0.1,
			"auto test: corner transparent after bg remove")

	# --- 项目数据（.tres，uid 关联，兼容 meta.json rects） ---
	var proj_data: SpriteSplitterData = _controller.data
	_check(proj_data != null and proj_data.project_name == "sheet",
			"auto test: data created with auto project name")
	_check(proj_data.sprites.size() == _controller.rects.size(),
			"auto test: data.sprites synced with rects (meta compatible)")
	# 保存按钮 emoji + 脏数据/保存成功状态
	var save_btn: Button = _side.get_node("VBox/ProjectRow/SaveBtn")
	_check(save_btn.text == "💾", "auto test: save button is emoji")
	_check(_controller.is_dirty, "auto test: dirty after split/param change")
	_controller.save_project("测试项目", _side._build_options(), "res://out_sprites/ui_test", 0)
	await get_tree().process_frame
	_check(not _controller.is_dirty, "auto test: clean after save")
	_check(String(_side.get("_save_state_label").text) == "✅ 已保存",
			"auto test: save success state shown")
	_check(FileAccess.file_exists(_controller.data_path),
			"auto test: data saved to tres")
	_check(_controller.data.source_texture != null,
			"auto test: data saved with source_texture (registry thumbnail source)")
	# 项目注册表（SpriteSplitterRegistry 自动注册 + UI 竖排列表）
	_check(_controller.registry != null, "auto test: registry initialized")
	_check(_controller.registry.has_entry(_controller.data_path),
			"auto test: saved data auto-registered")
	var reg_vbox: VBoxContainer = _main.get_node(
			"MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel/RegVBox/RegListArea/RegScroll/RegVBoxList")
	_check(reg_vbox.get_child_count() == _controller.registry.entries.size() * 2,
			"auto test: registry list shows all entries + separators")
	# 条目间分隔线：可复用 reg_sep.tscn（HSeparator，separator 主题项是 StyleBoxLine）
	var first_sep: Control = reg_vbox.get_child(1)
	_check(first_sep is HSeparator, "auto test: registry separator is HSeparator")
	var sep_sb: StyleBox = (first_sep as HSeparator).get_theme_stylebox("separator")
	_check(sep_sb is StyleBoxLine
			and (sep_sb as StyleBoxLine).color.is_equal_approx(Color(0.18538326, 0.18538326, 0.18538326, 1)),
			"auto test: registry separator stylebox color from tscn")
	# 首项竖排：缩略图 + 标题/uid/修改时间（reg_item 场景，顶级 PanelContainer）
	var first_item: Control = reg_vbox.get_child(0)
	var thumb_rect: TextureRect = first_item.get_thumb()
	_check(thumb_rect.texture != null and thumb_rect.texture.get_width() == 48,
			"auto test: registry thumbnail 48px")
	var reg_labels: Array[String] = []
	for l: Label in first_item.get_labels():
		reg_labels.append(l.text)
	_check(reg_labels.size() == 4 and reg_labels[0] == "测试项目",
			"auto test: registry item shows title")
	_check(reg_labels[1] != "", "auto test: registry item shows uid")
	_check(reg_labels[2].contains("修改于"), "auto test: registry item shows modified time")
	_check(reg_labels[3].begins_with("res://"),
			"auto test: registry item shows source path")
	# 打开图片后：按源图片 uid 自动同步注册表选中项（无需点击）
	_check(String(_main.get("_active_reg_path")) == _controller.data_path,
			"auto test: registry selection follows image uid (no click)")
	_check(first_item.selected, "auto test: registry item pre-selected by uid")
	_main._on_reg_item_clicked(_controller.data_path)
	_check(_controller.data != null and not _controller.data.project_name.is_empty(),
			"auto test: registry click loads config")
	_check(_controller.data.source_texture != null,
			"auto test: source_texture kept after registry click (apply_data sync)")
	_check(first_item.selected, "auto test: active registry item selected")
	# 选中样式回归：active 高亮 SEL_BG；取消后恢复 tscn 透明面板（set_selected 曾误删 tscn override）
	var sb_sel: StyleBox = first_item.get_theme_stylebox("panel")
	_check((sb_sel as StyleBoxFlat).bg_color.is_equal_approx(Color(0.152, 0.152, 0.181, 1.0)),
			"auto test: selected item highlighted with SEL_BG")
	first_item.call("set_selected", false)
	var sb_unsel: StyleBox = first_item.get_theme_stylebox("panel")
	_check((sb_unsel as StyleBoxFlat).bg_color.a == 0.0,
			"auto test: unselected item back to transparent panel (tscn override kept)")
	first_item.call("set_selected", true)
	# 注册表切换：脏数据 → 询问保存；保存并切换 / 取消保持
	_controller.mark_dirty()
	_main._on_reg_item_clicked(_controller.data_path)
	var sw_dlg: Variant = _main.get("_switch_dialog")
	_check(sw_dlg != null and sw_dlg.visible,
			"auto test: dirty switch shows save confirm")
	_main._on_switch_save()
	_check(not _controller.is_dirty, "auto test: switch-save persists current")
	_check(_controller.data != null, "auto test: switch-save loads entry")
	_controller.mark_dirty()
	_main._on_reg_item_clicked(_controller.data_path)
	_main._on_switch_cancel()
	_check(String(_main.get("_pending_switch_path")) == "",
			"auto test: cancel keeps current project")
	# 打开 SpriteSplitterData 配置按钮（主视图 Header）→ 弹对话框选择 .tres 应用
	var data_btn: Button = _main.get_node("MainVBox/Header/HeaderRow/DataBtn")
	_check(data_btn != null and data_btn.text == "打开配置",
			"auto test: data config button present")
	_main._on_open_data()
	_check(_main.get("_data_dialog") != null, "auto test: data dialog created")
	_main._on_data_selected(_controller.data_path)
	_check(_controller.data != null and _controller.data.project_name == "测试项目",
			"auto test: apply data config from dialog")
	_check(String(_main.get("_active_reg_path")) == _controller.data_path,
			"auto test: registry selection follows applied config path")
	_check(split_list.item_count == _controller.rects.size(),
			"auto test: split list refreshed after apply data")
	# 内置文件系统拖放（FileSystem dock → 预览区画布，带确认弹窗）
	var drop_img: Dictionary = {"type": "files",
			"files": PackedStringArray(["res://sprites/sheet.png"])}
	_check(canvas._can_drop_data(Vector2.ZERO, drop_img),
			"auto test: can drop image on canvas")
	canvas._drop_data(Vector2.ZERO, drop_img)
	var drop_dlg: Variant = _main.get("_drop_dialog")
	_check(drop_dlg != null and drop_dlg.visible, "auto test: drop shows confirm dialog")
	_main._on_drop_confirmed()
	_check(_controller.image != null and _controller.image_name == "sheet.png",
			"auto test: drop image confirmed loads image")
	var drop_tres: Dictionary = {"type": "files",
			"files": PackedStringArray([_controller.data_path])}
	_check(canvas._can_drop_data(Vector2.ZERO, drop_tres),
			"auto test: can drop tres on canvas")
	canvas._drop_data(Vector2.ZERO, drop_tres)
	_main._on_drop_confirmed()
	_check(_controller.data != null and _controller.data.project_name == "测试项目",
			"auto test: drop tres confirmed applies data")
	var bad_drop: Dictionary = {"type": "files",
			"files": PackedStringArray(["res://foo.gd"])}
	_check(not canvas._can_drop_data(Vector2.ZERO, bad_drop),
			"auto test: unsupported type not droppable")
	# 重新加载素材 → uid 关联恢复项目数据 + UI
	_check(_controller.load_image(DEFAULT_SHEET), "auto test: reload image")
	_check(_controller.data != null and _controller.data.project_name == "测试项目",
			"auto test: data reloaded by uid")
	_check(String(_side.get("_project_name_edit").text) == "测试项目",
			"auto test: project name restored to UI")
	_check(split_list.item_count == 0, "auto test: split list cleared on reload")

	# 布局健全性（主视图画布铺满 + 侧栏不溢出）
	_main_holder.size = Vector2(800, 600)   # headless 场景 root 仅 64x64，先给足尺寸
	await get_tree().process_frame
	_check(canvas.size.x > 0 and canvas.size.y > 0, "auto test: main canvas sized")
	_check(_side.get("_status_label") != null, "auto test: side panel wired")

	# 主视图 Header：打开素材 + 图片地址（加载后 label 更新）
	var header_label: Label = _main.get_node("MainVBox/Header/HeaderRow/FileLabel")
	_check(_main.get_node("MainVBox/Header/HeaderRow/FileButton") != null,
			"auto test: main header has file button")
	_check(String(header_label.text).contains("sheet.png"),
			"auto test: header label shows loaded name")
	# 关闭当前图片（Header CloseBtn → controller.close_image）
	var close_btn: Button = _main.get_node("MainVBox/Header/HeaderRow/CloseBtn")
	_check(close_btn != null and close_btn.text.contains("关闭"),
			"auto test: close button present")
	_main._on_close_image()
	_check(_controller.image == null and _controller.rects.is_empty(),
			"auto test: close image clears state")
	_check(String(header_label.text) == "未选择素材",
			"auto test: header label reset after close")
	_check(split_list.item_count == 0, "auto test: split list cleared after close")
	_check(split_empty.visible, "auto test: empty hint shown after close")
	_check(_controller.load_image(DEFAULT_SHEET), "auto test: reload after close")

	# 侧栏功能分组 tab（切分[含分析]/去背景/导出/导入；卡片样式主题色）
	var tabs: TabContainer = _side.get_node("VBox/TabContainer")
	_check(tabs.get_tab_count() == 4, "auto test: side tabs grouped (4)")
	_check(tabs.get_tab_title(0) == "切分" and tabs.get_tab_title(1) == "去背景"
			and tabs.get_tab_title(2) == "导出" and tabs.get_tab_title(3) == "导入",
			"auto test: tab titles set")
	tabs.current_tab = 2
	_check(tabs.current_tab == 2, "auto test: switch to export tab")
	# 导入按钮独立 tab（ImportTab，含导入卡片与按钮）
	_check(_side.get_node("VBox/TabContainer/ImportTab/VBox/ImportCard/ImportVBox/ImportMetaBtn") != null,
			"auto test: import meta button in own tab")
	# 分析并入切分 tab（分析按钮/参数/操作卡片）
	_check(_side.get_node("VBox/TabContainer/SplitTab/VBox/AnalyzeCard/AnalyzeVBox/AnalyzeBtn") != null,
			"auto test: analyze merged into split tab")
	_check(_side.get("_cards").size() == 6, "auto test: side has 6 themed cards")

	# 应用配置 → 统一入口（配套场景：配置素材=当前图 → 图片保持 + 区域加载）
	var cfg: SpriteSplitterData = _controller.data.duplicate()
	_controller.apply_data(cfg, "res://sps_data/_test_cfg.tres")
	_check(_controller.image_name == "sheet.png" and _controller.rects.size() == 64,
			"auto test: apply data keeps image and loads matching sprites")
	# 不配套场景：配置素材切到小图、区域超出 → 区域清空、图片切换
	var cfg2: SpriteSplitterData = _controller.data.duplicate()
	cfg2.source_image = "res://out_sprites/ui_test/sprite_01.png"
	_controller.apply_data(cfg2, "res://sps_data/_test_cfg2.tres")
	_check(_controller.image_name == "sprite_01.png",
			"auto test: apply data switches image")
	_check(_controller.rects.is_empty(),
			"auto test: mismatched sprites cleared")
	# 源文件更名模拟：配置里 source_image 失效 → 按 sheet_uid 反查 uid 缓存恢复加载并修复路径
	var cfg3: SpriteSplitterData = _controller.data.duplicate()
	cfg3.source_image = "res://sprites/renamed_not_exist.png"   # 旧路径已失效
	_controller.apply_data(cfg3, "res://sps_data/_test_cfg3.tres")
	_check(_controller.image_name == "sheet.png",
			"auto test: apply data resolves source via uid after rename")
	_check(cfg3.source_image == "res://sprites/sheet.png",
			"auto test: stale source_image repaired to current path")
	_check(_controller.load_image_by_uid(_controller.data.sheet_uid),
			"auto test: load image by uid resolves path")
	_check(_controller.image_name == "sheet.png",
			"auto test: load by uid loaded sheet")
	_check(not _controller.load_image_by_uid("uid://nonexistent000"),
			"auto test: load by invalid uid fails safely")
	# 重导入信号修复：注册表条目 source_image 失效 → _repair 用 uid 反查修复并持久化
	var stale: SpriteSplitterData = _controller.data.duplicate()
	stale.source_image = "res://sprites/renamed_not_exist.png"
	var stale_path: String = "res://sps_data_test/_stale_repair.tres"
	ResourceSaver.save(stale, stale_path)
	_controller.registry.register(stale_path)
	_controller._repair_stale_registry_paths()
	var stale2: Variant = load(stale_path)
	_check(stale2 is SpriteSplitterData
			and stale2.source_image == "res://sprites/sheet.png",
			"auto test: reimport repair fixes stale source_image (persisted)")
	_check(_controller.load_image(DEFAULT_SHEET), "auto test: restore sheet")
	# 去背景 → 写透明 PNG + 更新源（运行模式：路径更新，uid/纹理留待编辑器导入）
	await _controller.remove_background(12, "color", false, Color.WHITE, "http://127.0.0.1:8000")
	_check(FileAccess.file_exists("res://out_sprites/sheet_transparent.png"),
			"auto test: bg remove writes transparent png")
	_check(_controller.data.source_image == "res://out_sprites/sheet_transparent.png",
			"auto test: bg remove updates source_image")
	_check(_controller.image_res_path == "res://out_sprites/sheet_transparent.png",
			"auto test: bg remove updates image_res_path")

	print("[sps-ui] === auto test done (fail=%s) ===" % _fail)
	get_tree().quit(1 if _fail else 0)
