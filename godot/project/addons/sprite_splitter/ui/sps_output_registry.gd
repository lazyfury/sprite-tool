@tool
class_name SpriteOutputRegistry
extends Resource

## 生成资源注册表（.tres）：登记插件全部写盘产物（切分 PNG / meta.json /
## AtlasTexture .tres / 去背景 PNG / sheet PNG+meta），供清理窗口列出并检查占用。
## 默认存储：<data_dir>/output_registry.tres（随项目数据走，测试注入目录隔离）。
## 与 SpriteSplitterRegistry（登记项目配置 .tres）职责分离：本注册表只管"生成产物"。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

const KIND_PNG: String = "png"
const KIND_META: String = "meta"
const KIND_TRES: String = "tres"
const KIND_SHEET: String = "sheet"

@export var entries: Array[Dictionary] = []   # {path, kind, project, created_at, size}
@export var updated_at: int = 0


func has(path: String) -> bool:
	return find_index(path) >= 0


func find_index(path: String) -> int:
	for i: int in entries.size():
		if String(entries[i].get("path", "")) == path:
			return i
	return -1


# 登记一个生成资源；已存在则更新时间/大小（去重）。返回是否新增。
func register(path: String, kind: String, project: String = "") -> bool:
	if path.is_empty():
		return false
	var now: int = int(Time.get_unix_time_from_system())
	var size: int = _file_size(path)
	var idx: int = find_index(path)
	if idx >= 0:
		var e: Dictionary = entries[idx]
		e["kind"] = kind
		e["project"] = project
		e["created_at"] = now
		e["size"] = size
		return false
	entries.append({
		"path": path,
		"kind": kind,
		"project": project,
		"created_at": now,
		"size": size,
	})
	updated_at = now
	return true


func remove(path: String) -> bool:
	var idx: int = find_index(path)
	if idx < 0:
		return false
	entries.remove_at(idx)
	updated_at = int(Time.get_unix_time_from_system())
	return true


# 自愈：移除文件已不存在的条目（外部删除后清理窗口不显示幽灵项），返回移除数
func purge_missing() -> int:
	var kept: Array[Dictionary] = []
	var removed: int = 0
	for e: Dictionary in entries:
		if FileAccess.file_exists(String(e.get("path", ""))):
			kept.append(e)
		else:
			removed += 1
	if removed > 0:
		entries = kept
		updated_at = int(Time.get_unix_time_from_system())
	return removed


func _file_size(path: String) -> int:
	var fa: FileAccess = FileAccess.open(path, FileAccess.READ)
	if fa == null:
		return 0
	var n: int = fa.get_length()
	fa.close()
	return n
