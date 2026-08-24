extends Node

# SpriteSplitter GDExtension 冒烟测试。
# 所有预期值 = 同一素材在 CLI（sprite-split）上的 golden 输出，验证 Godot 层与 core 完全对齐：
#   1) sheet.png      auto 模式 → 64 个 16x16（素材为 16px 满格网格）
#   2) test_sheet.png components + min 2 → 3（噪点被过滤，M1 验收）
#   3) grid8_sheet.png grid cell 8 → 64（白底网格，全格非空）
#   4) grid8_sheet.png remove_background + auto → 5（M2 验收链路）
#   5) split_and_export / crop / export_sprite / 错误路径
#   9) 大图道具表（1234x1274 透明底）components+min37x26 → 80、auto → 80（CLI golden）
#  10) AtlasTexture（不切图）：读 meta.json 的 rect → 在大图上直接建 AtlasTexture
#      零文件输出、共享同一张纹理；校验 region/越界/重叠/像素与切图产物一致
#  11) AtlasTexture 保存为 .tres 资源并可重新加载
# 异步说明：测试在协程 _run_tests() 中分帧执行（重步骤间 await 让出帧循环），
# _ready() 立即返回 → 场景先渲染显示，不被测试阻塞。
# 退出码：全部通过 → 0；任一失败 → 1

var _fail := false

func _check(cond: bool, msg: String) -> void:
	if cond:
		print("[sps] PASS: ", msg)
	else:
		printerr("[sps] FAIL: ", msg)
		_fail = true

func _load_img(path: String) -> Image:
	return Image.load_from_file(path)

func _ready() -> void:
	print("[sps] === SpriteSplitter GDExtension smoke test ===")
	_run_tests()  # 协程异步跑；不 await → 本函数立即返回，场景先渲染

func _run_tests() -> void:
	var ss := SpriteSplitter.new()

	# --- 1) sheet.png auto（golden: 64 个 16x16）---
	var sheet := _load_img("res://sprites/sheet.png")
	_check(sheet != null, "load sprites/sheet.png")
	var rects: Array = ss.split(sheet, {"mode": "auto", "min_width": 2, "min_height": 2})
	print("[sps] sheet.png auto -> ", rects.size(), " rects")
	_check(rects.size() == 64, "sheet auto: 64 rects (got %d)" % rects.size())
	var all16 := true
	for r in rects:
		if not (r is Rect2i and r.size == Vector2i(16, 16)):
			all16 = false
	_check(all16, "sheet auto: all rects are 16x16")

	# --- 2) test_sheet.png components + min2（golden: 3）---
	var tsheet := _load_img("res://sprites/test_sheet.png")
	_check(tsheet != null, "load sprites/test_sheet.png")
	var crects: Array = ss.split(tsheet,
			{"mode": "components", "min_width": 2, "min_height": 2})
	print("[sps] test_sheet components -> ", crects.size(), " rects")
	_check(crects.size() == 3, "test_sheet components: 3 rects (got %d)" % crects.size())
	_check(crects.has(Rect2i(4, 4, 8, 8)), "first rect (4,4,8,8)")
	_check(crects.has(Rect2i(20, 4, 16, 8)), "second rect (20,4,16,8)")
	_check(crects.has(Rect2i(8, 20, 6, 10)), "third rect (8,20,6,10)")

	# --- 3) grid8_sheet.png grid cell 8（golden: 64）---
	var grid8 := _load_img("res://sprites/grid8_sheet.png")
	_check(grid8 != null, "load sprites/grid8_sheet.png")
	var grects: Array = ss.split(grid8, {"mode": "grid", "grid_cell_size": 8})
	print("[sps] grid8 grid -> ", grects.size(), " rects")
	_check(grects.size() == 64, "grid8 grid: 64 rects (got %d)" % grects.size())

	# --- 4) grid8 remove_background + auto（golden: 5）---
	var brects: Array = ss.split(grid8,
			{"mode": "auto", "remove_background": true})
	print("[sps] grid8 bg+auto -> ", brects.size(), " rects")
	_check(brects.size() == 5, "grid8 bg+auto: 5 rects (got %d)" % brects.size())

	# --- 5) analyze ---
	var stats: Dictionary = ss.analyze(sheet)
	print("[sps] analyze: ", stats)
	_check(stats.has("component_count") and stats.get("width", 0) == 128,
			"analyze size 128x128")

	# --- 6) split_and_export ---
	var files := ss.split_and_export(tsheet,
			{"mode": "components", "min_width": 2, "min_height": 2},
			"res://out_sprites")
	print("[sps] exported ", files.size(), " files")
	_check(files.size() == 3, "exported 3 pngs")
	for f in files:
		_check(FileAccess.file_exists(f), "file exists: %s" % f)
		var im := Image.load_from_file(f)
		_check(im != null and im.get_width() > 0 and im.get_height() > 0,
				"exported png readable: %s" % f)

	await get_tree().process_frame  # 让出帧：场景先渲染

	# --- 7) crop / export_sprite ---
	var c := ss.crop(sheet, Rect2i(0, 0, 32, 32))
	_check(c != null and c.get_width() == 32 and c.get_height() == 32, "crop 32x32")
	var err := ss.export_sprite(sheet, Rect2i(0, 0, 32, 32), "res://out_sprites/crop_test.png")
	_check(err == OK, "export_sprite OK")

	# --- 8) 错误路径 ---
	_check(ss.split(null, {}).is_empty(), "split(null) returns empty")
	_check(ss.split(sheet, {"mode": "bogus"}).size() > 0,
			"unknown mode falls back to components")
	_check(ss.export_sprite(sheet, Rect2i(0, 0, 999, 999), "res://out_sprites/big.png") == OK,
			"export clamps out-of-bounds rect")

	# --- 9) 大图道具表（1234x1274 透明底，用户素材；golden = CLI）---
	var big := _load_img("res://sprites/313b7493-30f7-433c-8578-15167424e1a1_transparent.png")
	_check(big != null, "load 313b7493..._transparent.png")
	await get_tree().process_frame  # 大 PNG 解码完成，让出帧
	if big != null:
		_check(big.get_width() == 1234 and big.get_height() == 1274, "big sheet size 1234x1274")
		# components + min 37x26（analyzer 推荐）→ 80 个 sprite（golden）
		var big_rects: Array = ss.split(big,
				{"mode": "components", "min_width": 37, "min_height": 26})
		print("[sps] big components+min37x26 -> ", big_rects.size(), " rects")
		_check(big_rects.size() == 80, "big components+min: 80 rects (got %d)" % big_rects.size())
		await get_tree().process_frame
		# auto（自动过滤碎片）→ 80（golden：无网格结构，回退 components + 自动 min）
		var arects: Array = ss.split(big, {"mode": "auto"})
		print("[sps] big auto -> ", arects.size(), " rects")
		_check(arects.size() == 80, "big auto: 80 rects (got %d)" % arects.size())
		# 默认 min1 → 碎片（golden: 4203），验证过滤效果对比
		var raw: Array = ss.split(big, {"mode": "components"})
		_check(raw.size() > big_rects.size(), "big raw fragments > filtered (%d > %d)" % [raw.size(), big_rects.size()])
		await get_tree().process_frame  # 4203 组件检测完成，让出帧
		# 写文件链路：导出 80 个精灵 PNG + meta.json（与 CLI 同源格式）
		var big_out := "res://out_sprites/big"
		var pngs := ss.split_and_export(big,
				{"mode": "components", "min_width": 37, "min_height": 26}, big_out)
		print("[sps] big exported pngs -> ", pngs.size())
		_check(pngs.size() == 80, "big exported 80 pngs (got %d)" % pngs.size())
		var all_png_ok := true
		for f in pngs:
			if not FileAccess.file_exists(f):
				all_png_ok = false
				printerr("[sps] missing png: ", f)
		_check(all_png_ok, "big all 80 png files exist")
		await get_tree().process_frame  # 80 PNG 落盘完成，让出帧
		var meta_path := big_out + "/meta.json"
		var meta_err = ss.export_metadata(big, big_rects,
				"313b7493-30f7-433c-8578-15167424e1a1_transparent.png", meta_path)
		_check(meta_err == OK, "big export_metadata OK (err %d)" % meta_err)
		if FileAccess.file_exists(meta_path):
			var j: Variant = JSON.parse_string(FileAccess.get_file_as_string(meta_path))
			_check(j is Dictionary, "meta.json parses as Dictionary")
			if j is Dictionary:
				_check(j.get("sprites", []).size() == 80,
						"meta.json has 80 sprites (got %d)" % j.get("sprites", []).size())
				_check(j.get("width", 0) == 1234 and j.get("height", 0) == 1274,
						"meta.json size 1234x1274")
				# 与 CLI 输出逐项核对首个 sprite 坐标（golden: 1098,27,99,122）
				var sprites: Array = j.get("sprites", [])
				if sprites.size() > 0:
					var first: Dictionary = sprites[0]
					_check(first.get("x", -1) == 1098 and first.get("y", -1) == 27
							and first.get("width", -1) == 99 and first.get("height", -1) == 122,
							"meta.json first sprite (1098,27,99,122) got %s" % first)
		else:
			_check(false, "meta.json file written")

	# --- 10) AtlasTexture（不切图）：meta.json 区域 → AtlasTexture ---
	# 与 9) 的"切 80 个 PNG"对比：不产生任何文件，直接用 meta.json 的 rect
	# 在大图上构建 AtlasTexture（atlas 共享同一张纹理，省内存/省 IO，图集方案）。
	var at_meta := "res://sprites/meta.json"
	var at_mj: Variant = null
	if FileAccess.file_exists(at_meta):
		at_mj = JSON.parse_string(FileAccess.get_file_as_string(at_meta))
	_check(at_mj is Dictionary, "atlas meta.json parses")
	if at_mj is Dictionary:
		var ms: Array = at_mj.get("sprites", [])
		var mw: int = at_mj.get("width", 0)
		var mh: int = at_mj.get("height", 0)
		_check(mw == 1234 and mh == 1274, "atlas meta size 1234x1274 (got %dx%d)" % [mw, mh])
		_check(ms.size() == 80, "atlas meta 80 sprites (got %d)" % ms.size())
		var at_img := _load_img("res://sprites/313b7493-30f7-433c-8578-15167424e1a1_transparent.png")
		_check(at_img != null, "load big sheet for atlas")
		await get_tree().process_frame  # 大图解码完成，让出帧
		if at_img != null:
			var at_tex := ImageTexture.create_from_image(at_img)
			await get_tree().process_frame  # 纹理上传 GPU 完成，让出帧
			var atlases: Array[AtlasTexture] = []
			var oob := 0
			var overlap := 0
			var used: Array[Rect2i] = []
			for s in ms:
				var r := Rect2i(s.get("x", -1), s.get("y", -1),
						s.get("width", -1), s.get("height", -1))
				if r.position.x < 0 or r.position.y < 0 or r.size.x <= 0 or r.size.y <= 0 \
						or r.end.x > mw or r.end.y > mh:
					oob += 1
					printerr("[sps] atlas region out of bounds: ", r)
					continue
				for u in used:
					if r.intersects(u):
						overlap += 1
				used.append(r)
				var at := AtlasTexture.new()
				at.atlas = at_tex
				at.region = r
				atlases.append(at)
			_check(atlases.size() == 80, "80 AtlasTextures built (got %d)" % atlases.size())
			_check(oob == 0, "0 regions out of bounds (got %d)" % oob)
			_check(overlap == 0, "0 overlapping regions (got %d)" % overlap)
			# 首个区域与 meta golden 一致（1098,27,99,122）
			var f := atlases[0]
			_check(f.region.position == Vector2(1098, 27) and f.region.size == Vector2(99, 122),
					"atlas[0] region 1098,27,99,122 (got %s)" % f.region)
			# 像素级验证：AtlasTexture.get_image() 应等于切图产物 sprite_01.png（同图同区域）
			var at_img0 := f.get_image()
			var cut0 := _load_img("res://out_sprites/big/sprite_01.png")
			if at_img0 != null and cut0 != null:
				_check(at_img0.get_width() == cut0.get_width()
						and at_img0.get_height() == cut0.get_height(),
						"atlas[0] image %dx%d == sprite_01 %dx%d"
						% [at_img0.get_width(), at_img0.get_height(),
							cut0.get_width(), cut0.get_height()])
				_check(at_img0.get_data() == cut0.get_data(),
						"atlas[0] pixels identical to sprite_01.png")
			else:
				_check(false, "atlas[0] get_image() and sprite_01.png readable")

	# --- 11) AtlasTexture 保存为资源（.tres）并可重新加载 ---
	# AtlasTexture 是 Resource，可 ResourceSaver 落盘复用（编辑器可拖进场景）。
	# 注意：atlas 纹理必须是走导入管线的引用（load()），动态 ImageTexture
	# 无法内联进 .tres 文本资源（保存会失败或丢失引用）。
	var atlas_src := load(
			"res://sprites/313b7493-30f7-433c-8578-15167424e1a1_transparent.png") as Texture2D
	_check(atlas_src != null, "load imported sheet texture (atlas save)")
	if atlas_src != null and at_mj is Dictionary:
		var ams: Array = at_mj.get("sprites", [])
		var atlas_dir := "res://out_sprites/atlas"
		DirAccess.make_dir_recursive_absolute(atlas_dir)
		var tres_ok := true
		for i in range(mini(3, ams.size())):
			var s: Dictionary = ams[i]
			var at := AtlasTexture.new()
			at.atlas = atlas_src
			at.region = Rect2i(s.get("x", 0), s.get("y", 0),
					s.get("width", 0), s.get("height", 0))
			var p := "%s/atlas_%02d.tres" % [atlas_dir, i + 1]
			if ResourceSaver.save(at, p) != OK:
				tres_ok = false
				printerr("[sps] atlas save failed: ", p)
				continue
			var loaded := load(p) as AtlasTexture
			if loaded == null or loaded.region != at.region:
				tres_ok = false
				printerr("[sps] atlas reload mismatch: ", p)
				continue
			_check(loaded.atlas != null
					and loaded.atlas.resource_path == atlas_src.resource_path,
					"reloaded %s atlas ref %s"
					% [p.get_file(), loaded.atlas.resource_path.get_file()])
		await get_tree().process_frame  # 资源落盘完成，让出帧
		_check(tres_ok, "3 AtlasTextures saved + reloaded as .tres")

	print("[sps] === done (fail=%s) ===" % _fail)
	#get_tree().quit(1 if _fail else 0)
