class_name AlterStatEffect
extends ItemEffect

@export_enum("damage", "armor", "attack_speed", "speed") var stat: String = "damage"
@export var additive: float = 0.0
@export_range(0.0, 10.0, 0.05) var multiplier: float = 1.0
