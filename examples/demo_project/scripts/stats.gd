class_name Stats
extends Resource
## Numeric stats granted by an item.

@export_range(0, 100) var damage: int = 0
@export_range(0, 100) var armor: int = 0
@export_range(0.1, 5.0, 0.1) var attack_speed: float = 1.0
@export_range(-50, 50, 1, "suffix:%") var move_speed_bonus: int = 0
