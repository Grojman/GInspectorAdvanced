class_name EnemyData
extends RefCounted

enum Behavior { CHASE, SHOOT, CHARGE }

@export var name: String
@export_range(1, 1000) var health: int = 10
@export var speed: float = 100.0
@export var behavior: Behavior = Behavior.CHASE
@export var body_size := Vector2(14, 11)
@export var loot: Dictionary[String, float] = {}
