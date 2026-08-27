@tool
class_name SpriteSplitterRegistry
extends Resource

## sprite-tool 项目注册表（.tres）：登记所有已保存的 SpriteSplitterData 路径。
## 默认注册表：res://sps_data/registry.tres（与项目数据同目录，随项目走）；
## 新建/保存项目数据时自动注册，UI 注册表列表据此展示并支持点击加载。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

@export var entries: Array[String] = []   # 已注册的 data 路径（res://sps_data/xxx.tres）
@export var updated_at: int = 0


func has_entry(path: String) -> bool:
	return path in entries


# 注册一个 data 路径，去重；返回是否新增
func register(path: String) -> bool:
	if path in entries:
		return false
	entries.append(path)
	updated_at = int(Time.get_unix_time_from_system())
	return true
