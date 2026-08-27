@tool
extends Control

## SpriteSheetBuilder 独立测试（headless）：验证新类 build / save_sheet 与 CLI sheet
## 行为一致（重排网格、sheet.png + sheet_meta.json、src/dst 映射、边界防御）。
## 与 test_harness 解耦——新增类独立验证，不污染既有 79 断言。
##
## 运行：Godot --headless --path . res://addons/sprite_tool/ui/sheet_test.tscn -- --sps-sheet-test
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

const FLAG: String = "--sps-sheet-test"
const SHEET: String = "res://sprites/sheet.png"
const OUT_DIR: String = "res://out_sheet_test"
const DATA_DIR2: String = "res://sps_data_test2"
const SIDE_SCENE: PackedScene = preload("res://addons/sprite_tool/ui/sprite_tool_side.tscn")
const SHEET_WINDOW_SCENE: PackedScene = preload("res://addons/sprite_tool/ui/sheet_builder_window.tscn")
const COLS: int = 8
const PADDING: int = 4

var _pass: int = 0
var _fail: int = 0


func _ready() -> void:
	if not OS.get_cmdline_user_args().has(FLAG):
		return   # 编辑器/其他场景下打开不自动跑
	_run()


func _check(cond: bool, label: String) -> void:
	if cond:
		_pass += 1
		print("  [PASS] ", label)
	else:
		_fail += 1
		print("  [FAIL] ", label)


func _clean_out() -> void:
	var dir: DirAccess = DirAccess.open(OUT_DIR)
	if dir == null:
		return
	dir.list_dir_begin()
	var f: String = dir.get_next()
	while not f.is_empty():
		if not dir.current_is_dir():
			DirAccess.remove_absolute(OUT_DIR + "/" + f)
		f = dir.get_next()
	dir.list_dir_end()


func _run() -> void:
	print("== SpriteSheetBuilder test ==")
	_clean_out()

	# 1) 素材 + 自动切分拿 rects（复用 SpriteSplitter，验证交叉可用）
	var img: Image = Image.load_from_file(SHEET)
	_check(img != null and img.get_width() > 0, "load sheet.png")
	if img == null:
		_finish()
		return
	var splitter: SpriteSplitter = SpriteSplitter.new()
	var rects: Array = splitter.split(img, {"mode": "auto"})
	_check(rects.size() > 0, "auto split found %d rects" % rects.size())
	if rects.is_empty():
		_finish()
		return

	var ssb: SpriteSheetBuilder = SpriteSheetBuilder.new()

	# 2) build：内存重排
	var r: Dictionary = ssb.build(img, rects, COLS, PADDING)
	_check(not r.is_empty(), "build non-empty")
	var sheet: Image = r["sheet"] if r.has("sheet") else null
	var new_rects: Array = r["rects"] if r.has("rects") else []
	_check(sheet != null and sheet.get_width() > 0 and sheet.get_height() > 0, "build sheet size > 0")
	_check(new_rects.size() == rects.size(), "rects count preserved (%d)" % new_rects.size())
	if new_rects.size() == rects.size():
		# 网格几何：cols 列 → 行数 = ceil(n/cols)
		var rows: int = int(ceil(float(new_rects.size()) / COLS))
		_check(int(rects.size()) <= COLS * rows, "grid rows cover all sprites")
		# dst 全部落在 sheet 内
		var in_bounds: bool = true
		for nr: Rect2i in new_rects:
			if nr.end.x > sheet.get_width() or nr.end.y > sheet.get_height() \
					or nr.position.x < 0 or nr.position.y < 0:
				in_bounds = false
		_check(in_bounds, "all dst rects in bounds")

		# 3) 像素校验：重排 = 纯平移，抽 3 点比色
		_check_pixels(img, rects[0], sheet, new_rects[0])

	# 4) save_sheet：导出 sheet.png + sheet_meta.json
	var d: Dictionary = ssb.save_sheet(img, rects, COLS, PADDING, OUT_DIR)
	_check(not d.is_empty(), "save_sheet non-empty")
	_check(FileAccess.file_exists(OUT_DIR + "/sheet.png"), "sheet.png written")
	_check(FileAccess.file_exists(OUT_DIR + "/sheet_meta.json"), "sheet_meta.json written")
	if not d.is_empty():
		_check(int(d.get("count", -1)) == rects.size(), "save_sheet count == %d" % rects.size())
		var meta_text: String = FileAccess.get_file_as_string(OUT_DIR + "/sheet_meta.json")
		var j: Variant = JSON.parse_string(meta_text)
		_check(j is Dictionary, "meta.json parses")
		if j is Dictionary:
			_check(int(j.get("width", 0)) == int(d.get("width", 0)), "meta width matches")
			_check(int(j.get("height", 0)) == int(d.get("height", 0)), "meta height matches")
			var sprites: Array = j.get("sprites", [])
			_check(sprites.size() == rects.size(), "meta sprites count")
			if sprites.size() == rects.size() and not sprites.is_empty():
				var s0: Dictionary = sprites[0]
				_check(s0.has("src") and s0.has("dst"), "meta sprite[0] has src/dst")
				var src0: Dictionary = s0.get("src", {})
				var r0: Rect2i = rects[0]
				_check(int(src0.get("x", -1)) == r0.position.x and int(src0.get("y", -1)) == r0.position.y,
						"meta src matches input rect")

	# 5) 边界防御
	_check(ssb.build(img, [], COLS, PADDING).is_empty(), "empty rects -> empty dict")
	_check(ssb.build(img, rects, 0, PADDING).is_empty(), "cols<=0 -> empty dict")
	_check(ssb.save_sheet(img, rects, COLS, PADDING, "").is_empty(), "empty out_dir -> empty dict")

	# 5.5) 固定格宽高：128×128 统一格子（8x8 精灵不裁剪）+ 超格裁剪 + grid 宽高分离
	var r_fixed: Dictionary = ssb.build(img, rects, COLS, PADDING, 128, 128)
	_check(not r_fixed.is_empty(), "fixed cell build non-empty")
	if not r_fixed.is_empty():
		var sheet_fixed: Image = r_fixed["sheet"]
		_check(sheet_fixed.get_width() == 8 * 128 and sheet_fixed.get_height() == 8 * 128,
				"fixed cell sheet 1024x1024 (got %dx%d)" % [sheet_fixed.get_width(), sheet_fixed.get_height()])
		_check(int(r_fixed.get("clipped", -1)) == 0, "8x8 sprites in 128 cells -> clipped 0")
	var r_clip: Dictionary = ssb.build(img, rects, COLS, PADDING, 4, 4)
	_check(int(r_clip.get("clipped", -1)) == 64, "8x8 sprites in 4x4 cells -> clipped 64")
	var d_fixed: Dictionary = ssb.save_sheet(img, rects, COLS, PADDING, OUT_DIR, 128, 128)
	_check(int(d_fixed.get("count", -1)) == 64 and int(d_fixed.get("clipped", -1)) == 0,
			"fixed cell save_sheet ok (count/clipped)")
	# 5.6) 文件命名：自定义 stem / 同名递增另存
	var d_name: Dictionary = ssb.save_sheet(img, rects, COLS, PADDING, OUT_DIR, 0, 0, "my_hero", true)
	_check(String(d_name.get("sheet_path", "")).ends_with("/my_hero.png") \
			and FileAccess.file_exists(OUT_DIR + "/my_hero.png"), "custom stem -> my_hero.png")
	_check(FileAccess.file_exists(OUT_DIR + "/my_hero_meta.json"), "custom stem meta written")
	var d_inc1: Dictionary = ssb.save_sheet(img, rects, COLS, PADDING, OUT_DIR, 0, 0, "my_hero", false)
	_check(String(d_inc1.get("sheet_path", "")).ends_with("/my_hero_2.png"), "overwrite=false -> my_hero_2.png")
	var d_inc2: Dictionary = ssb.save_sheet(img, rects, COLS, PADDING, OUT_DIR, 0, 0, "my_hero", false)
	_check(String(d_inc2.get("sheet_path", "")).ends_with("/my_hero_3.png"), "overwrite=false -> my_hero_3.png")
	# 5.7) 从多张已切分小图组装（build_from_images / save_from_images）
	var img_a: Image = Image.create(10, 10, false, Image.FORMAT_RGBA8)
	img_a.fill(Color.RED)
	var img_b: Image = Image.create(20, 15, false, Image.FORMAT_RGBA8)
	img_b.fill(Color.BLUE)
	var imgs: Array[Image] = [img_a, img_b]
	var r_bi: Dictionary = ssb.build_from_images(imgs, 2, 0)
	_check(not r_bi.is_empty(), "build_from_images non-empty")
	if not r_bi.is_empty():
		var sheet_bi: Image = r_bi["sheet"]
		_check(sheet_bi.get_width() == 40 and sheet_bi.get_height() == 15,
				"from_images adaptive cell 20x15 -> 40x15")
		_check(r_bi["rects"].size() == 2, "from_images rects count")
	var d_bi: Dictionary = ssb.save_from_images(imgs, 2, 0, OUT_DIR, 0, 0, "merged", true,
			["frag_a.png", "frag_b.png"])
	_check(FileAccess.file_exists(OUT_DIR + "/merged.png"), "save_from_images png written")
	_check(FileAccess.file_exists(OUT_DIR + "/merged_meta.json"), "save_from_images meta written")
	var mj: Variant = JSON.parse_string(FileAccess.get_file_as_string(OUT_DIR + "/merged_meta.json"))
	_check(mj is Dictionary and (mj as Dictionary).get("src_files", []).size() == 2,
			"meta has src_files (2)")
	_check(ssb.build_from_images([], 2, 0).is_empty(), "empty images -> empty dict")
	# grid 宽高分离（16×32 非正方形格子）+ 正方形兜底（grid_cell_size）
	var splitter2: SpriteSplitter = SpriteSplitter.new()
	var grects: Array = splitter2.split(img, {"mode": "grid", "grid_cell_w": 16, "grid_cell_h": 32})
	var g_ok: bool = grects.size() == 32
	for gr: Rect2i in grects:
		if gr.size != Vector2i(16, 32):
			g_ok = false
	_check(g_ok, "grid 16x32 cells -> 32 rects of 16x32 (got %d)" % grects.size())
	var g_sq: Array = splitter2.split(img, {"mode": "grid", "grid_cell_size": 16})
	_check(g_sq.size() == 64, "grid square 16 fallback -> 64 rects")

	# 6) controller 级：build_sheet_preview / export_sheet（隔离数据目录，不碰用户数据）
	_test_controller(img)

	# 7) UI 级：side 面板 SheetTab 存在 + tab 标题
	await _test_side_tab()

	# 8) UI 级：创建 Sheets 独立窗口（多图组装）
	await _test_sheet_window()

	_finish()


# controller 集成：预览 + 导出走 SpsController（隔离 data_dir，验证信号/文件系统刷新不崩）
func _test_controller(img: Image) -> void:
	_clean_data_dir(DATA_DIR2)
	DirAccess.make_dir_recursive_absolute(DATA_DIR2)   # registry.tres 落盘需目录存在
	var ctrl: SpsController = SpsController.new(get_tree(), DATA_DIR2)
	_check(ctrl.sheet_builder != null, "controller has sheet_builder")
	ctrl.load_image(SHEET)
	ctrl.split({"mode": "auto"})
	_check(ctrl.sprites.size() == 64, "controller split -> %d sprites" % ctrl.sprites.size())
	var r2: Dictionary = ctrl.build_sheet_preview(COLS, PADDING)
	_check(not r2.is_empty() and r2.has("sheet"), "controller build_sheet_preview")
	_check(ctrl.build_sheet_preview(COLS, PADDING).is_empty() == false, "preview idempotent")
	var d2: Dictionary = ctrl.export_sheet(COLS, PADDING, OUT_DIR)
	_check(not d2.is_empty(), "controller export_sheet")
	_check(int(d2.get("count", -1)) == 64, "controller export count == 64")
	_check(FileAccess.file_exists(OUT_DIR + "/sheet_meta.json"), "controller meta written")
	# controller 固定格：128 无裁剪 / 4x4 全裁剪
	var d3: Dictionary = ctrl.export_sheet(COLS, PADDING, OUT_DIR, 128, 128)
	_check(int(d3.get("count", -1)) == 64 and int(d3.get("clipped", -1)) == 0,
			"controller fixed cell export ok")
	var r3: Dictionary = ctrl.build_sheet_preview(COLS, PADDING, 4, 4)
	_check(int(r3.get("clipped", -1)) == 64, "controller clipped count == 64")
	# 命名：自动 <源名>_sheet + 递增另存
	var dn: Dictionary = ctrl.export_sheet(COLS, PADDING, OUT_DIR)
	_check(String(dn.get("sheet_path", "")).ends_with("sheet_sheet.png"),
			"controller auto stem <源名>_sheet")
	var dn2: Dictionary = ctrl.export_sheet(COLS, PADDING, OUT_DIR, 0, 0, "", false)
	_check(String(dn2.get("sheet_path", "")).contains("sheet_sheet_2.png"),
			"controller overwrite=false -> _2")
	# 多图组装：先生成两张测试小图再走 controller
	var fa: Image = Image.create(12, 12, false, Image.FORMAT_RGBA8)
	fa.fill(Color.GREEN)
	var fb: Image = Image.create(12, 12, false, Image.FORMAT_RGBA8)
	fb.fill(Color.YELLOW)
	fa.save_png(OUT_DIR + "/frag_a.png")
	fb.save_png(OUT_DIR + "/frag_b.png")
	var df: Dictionary = ctrl.export_sheet_from_files(
			[OUT_DIR + "/frag_a.png", OUT_DIR + "/frag_b.png"], 2, 0, OUT_DIR, 0, 0, "frag_sheet", true)
	_check(FileAccess.file_exists(OUT_DIR + "/frag_sheet.png"), "controller save_from_files png")
	_check(int(df.get("count", -1)) == 2, "controller save_from_files count == 2")
	# 注册表删除当前资源配置 → 图片关闭（画布/切片/项目数据清空）
	var cur_path: String = ctrl.data_path
	_check(ctrl.image != null and not cur_path.is_empty(), "image+data open before delete")
	ctrl.remove_registry_entry(cur_path)
	_check(ctrl.image == null and ctrl.data == null and ctrl.data_path.is_empty(),
			"remove current entry closes image & data")
	_check(ctrl.sprites.is_empty(), "sprites cleared after remove")
	# 无图 / 无切片防御
	ctrl.close_image()
	_check(ctrl.build_sheet_preview(COLS, PADDING).is_empty(), "no image -> preview empty")
	_check(ctrl.export_sheet(COLS, PADDING, OUT_DIR).is_empty(), "no image -> export empty")
	_clean_data_dir(DATA_DIR2)


# UI 级：实例化 side 场景，验证 SheetTab 已挂载且标题正确
func _test_side_tab() -> void:
	var side: Control = SIDE_SCENE.instantiate()
	add_child(side)
	await get_tree().process_frame
	var tabs: TabContainer = side.get_node("VBox/TabContainer")
	_check(tabs != null and tabs.get_tab_count() == 5, "side has 5 tabs")
	_check(tabs.get_tab_title(4) == "Sheet", "tab[4] title == Sheet")
	_check(side.get_node("VBox/TabContainer/SheetTab") != null, "SheetTab node exists")
	_check(side.get_node("VBox/TabContainer/SheetTab/VBox/SheetCard/SheetVBox/SheetExportBtn") != null,
			"SheetExportBtn exists")
	_check(side.get_node("VBox/TabContainer/SheetTab/VBox/SheetCard/SheetVBox/SheetPreviewBtn") != null,
			"SheetPreviewBtn exists")
	# 多图组装功能已移至独立窗口（Sheet tab 不再有文件选择按钮）
	_check(side.get_node_or_null("VBox/TabContainer/SheetTab/VBox/SheetCard/SheetVBox/SheetFilesBtn") == null,
			"side sheet tab has no files btn (moved to window)")
	side.queue_free()
	await get_tree().process_frame


# 创建 Sheets 独立窗口：节点存在 + 选择图片对话框 + 选中回调更新 + 源图预览
func _test_sheet_window() -> void:
	var win: Window = SHEET_WINDOW_SCENE.instantiate()
	add_child(win)
	await get_tree().process_frame
	_check(win.get_node("Scroll/VBox/MarginContainer/FilesRow/FilesBtn") != null, "window files btn exists")
	_check(win.get_node("Scroll/VBox/Split/RightPanel/ActionCard/ActionVBox/PreviewBtn") != null, "window preview btn exists")
	_check(win.get_node("Scroll/VBox/Split/RightPanel/ActionCard/ActionVBox/ExportBtn") != null, "window export btn exists")
	_check(win.get_node("Scroll/VBox/Split/LeftPanel/LeftVBox/SourceList") != null, "window source list exists")
	var files_btn: Button = win.get_node("Scroll/VBox/MarginContainer/FilesRow/FilesBtn")
	files_btn.pressed.emit()
	await get_tree().process_frame
	var dlg: Node = null
	for child: Node in win.get_children():
		if child is FileDialog:
			dlg = child
	_check(dlg != null, "window files dialog created on click")
	var files_label: Label = win.get_node("Scroll/VBox/MarginContainer/FilesRow/FilesLabel")
	win._on_files_selected(PackedStringArray([OUT_DIR + "/frag_a.png", OUT_DIR + "/frag_b.png"]))
	_check(String(files_label.text).contains("2 张"), "window files label updated after selection")
	var src_list: ItemList = win.get_node("Scroll/VBox/Split/LeftPanel/LeftVBox/SourceList")
	_check(src_list.item_count == 2, "window source list shows 2 items")
	# 上移/下移排序：选中第 2 项上移 → 顺序交换；边界项不可越界
	win._source_list.select(1)
	win._on_move_up()
	_check(win._files[0].ends_with("frag_b.png") and win._files[1].ends_with("frag_a.png"),
			"move up swaps order")
	win._source_list.select(0)
	win._on_move_down()
	_check(win._files[0].ends_with("frag_a.png") and win._files[1].ends_with("frag_b.png"),
			"move down restores order")
	win._source_list.select(0)
	win._on_move_up()
	_check(win._files[0].ends_with("frag_a.png"), "first item cannot move up")
	# 🎲 随机文件名按钮 / 🔄 重新开始按钮
	var old_name: String = String(win._file_name.text)
	win._on_random_name_btn()
	_check(String(win._file_name.text).begins_with("sheet_") \
			and String(win._file_name.text) != old_name, "random btn regenerates name")
	win._on_reset_btn()
	_check(win._files.is_empty() and win._source_list.item_count == 0,
			"reset clears files & list")
	_check(String(win._file_name.text).begins_with("sheet_"), "reset regenerates random name")
	_check(not win._preview_tex.visible, "reset hides preview")
	# 随机文件名：格式 sheet_xxxxxx（headless 无窗口系统，直接测生成函数）
	var nm: String = win._random_name()
	_check(nm.begins_with("sheet_") and nm.length() == 12, "random name format sheet_xxxxxx")
	win.reset_file_name()   # main 每次 popup 前显式调用
	_check(String(win._file_name.text).begins_with("sheet_"), "reset_file_name fills random name")
	# 随机名可用性：连续生成不冲突（png/meta 均不存在）
	win._out_dir.text = OUT_DIR
	var all_free: bool = true
	for i in 24:
		var nm2: String = win._random_name()
		if FileAccess.file_exists(OUT_DIR + "/" + nm2 + ".png") \
				or FileAccess.file_exists(OUT_DIR + "/" + nm2 + "_meta.json"):
			all_free = false
	_check(all_free, "random names never collide (24 tries)")
	# name_in_use 检测：预占文件 → 判定冲突
	var tmp_img: Image = Image.create(4, 4, false, Image.FORMAT_RGBA8)
	tmp_img.fill(Color.GRAY)
	tmp_img.save_png(OUT_DIR + "/zz_collide.png")
	_check(win._name_in_use(OUT_DIR, "zz_collide"), "name_in_use detects existing png")
	_check(not win._name_in_use(OUT_DIR, "zz_free_xyz"), "name_in_use false for free name")
	# 文件已存在 → 禁止写入（拦截提示，不导出）；勾「另存为」→ 递增不覆盖
	var ctrl2: SpsController = SpsController.new(get_tree(), DATA_DIR2)
	DirAccess.make_dir_recursive_absolute(OUT_DIR)
	for f: String in ["blocked.png", "blocked_2.png", "blocked_3.png"]:
		if FileAccess.file_exists(OUT_DIR + "/" + f):
			DirAccess.remove_absolute(OUT_DIR + "/" + f)
	var blocker: Image = Image.create(8, 8, false, Image.FORMAT_RGBA8)
	blocker.fill(Color.WHITE)
	blocker.save_png(OUT_DIR + "/blocked.png")
	_check(FileAccess.file_exists(OUT_DIR + "/blocked.png"), "blocked.png created for test")
	win.set_controller(ctrl2)
	win._on_files_selected(PackedStringArray([OUT_DIR + "/frag_a.png", OUT_DIR + "/frag_b.png"]))
	win._file_name.text = "blocked"
	win._overwrite_check.button_pressed = false
	win._on_export()
	_check(String(win._result_label.text).contains("禁止覆盖"), "existing name -> forbid overwrite")
	_check(not String(win._result_label.text).contains("已导出"), "no export on blocked name")
	# 空文件名（随机名失效兜底）：默认 sheet 名也参与防覆盖检查
	win._file_name.text = ""
	win._overwrite_check.button_pressed = false
	win._out_dir.text = OUT_DIR
	win._on_export()
	_check(String(win._result_label.text).contains("禁止覆盖"), "empty name -> forbid sheet.png overwrite")
	win._overwrite_check.button_pressed = true
	win._file_name.text = "blocked"
	win._out_dir.text = OUT_DIR   # 窗口默认 res://out_sheet，测试指定隔离目录
	win._on_export()
	_check(FileAccess.file_exists(OUT_DIR + "/blocked_2.png"), "overwrite-check on -> increment blocked_2")
	win.queue_free()
	await get_tree().process_frame


func _clean_data_dir(dir: String) -> void:
	var d: DirAccess = DirAccess.open(dir)
	if d == null:
		return
	d.list_dir_begin()
	var f: String = d.get_next()
	while not f.is_empty():
		if f.ends_with(".tres"):
			DirAccess.remove_absolute(dir + "/" + f)
		f = d.get_next()
	d.list_dir_end()


# 平移不变性：dst 像素 == 原图对应像素（dx/dy 平移量由 src->dst 推导）
func _check_pixels(src_img: Image, src: Rect2i, sheet: Image, dst: Rect2i) -> void:
	var dx: int = dst.position.x - src.position.x
	var dy: int = dst.position.y - src.position.y
	var pts: Array = [
		[src.position.x, src.position.y],
		[src.position.x + src.size.x / 2, src.position.y + src.size.y / 2],
		[src.position.x + src.size.x - 1, src.position.y + src.size.y - 1],
	]
	var matched: int = 0
	for p: Array in pts:
		var a: Color = src_img.get_pixel(int(p[0]), int(p[1]))
		var b: Color = sheet.get_pixel(int(p[0]) + dx, int(p[1]) + dy)
		if a.is_equal_approx(b):
			matched += 1
	_check(matched == pts.size(),
			"pixel preserved (dst = src + (%d,%d), %d/%d)" % [dx, dy, matched, pts.size()])


func _finish() -> void:
	print("== SpriteSheetBuilder result: %d passed, %d failed ==" % [_pass, _fail])
	get_tree().quit(0 if _fail == 0 else 1)
