class_name ItemData
extends Resource
## An item of the demo roguelike, read from data/items.json (id -> item).

enum Rarity { COMMON, UNCOMMON, RARE, LEGENDARY = 10 }

const DEFAULT_PRICE := 15

## Filled from the dictionary key when the item is loaded.
var id: StringName
@export var display_name: String = "Unnamed"
@export_multiline var description: String
@export_file("*.png", "*.svg") var icon: String
@export var rarity: Rarity = Rarity.COMMON
## What it costs in a shop.
@export_range(0, 999, 1, "or_greater") var price := DEFAULT_PRICE
@export_range(0.0, 1.0, 0.05) var drop_chance: float = 0.25
@export var tint := Color(1, 1, 1)
@export_enum("Weapon", "Armor", "Consumable", "Trinket") var category: String = "Trinket"
@export_flags("Fire", "Water", "Earth", "Air") var elements := 0
@export var stack_size: int = 1
@export var tags: Array[String] = []

@export_group("Stats")
@export var stats: Stats
@export var effects: Array[ItemEffect] = []

@export_group("Presentation")
@export var pickup_offset := Vector2(0, -8)
@export var sprite_parts: Array[SpritePart] = []

var _cached_owner: Node  # private: hidden from the inspector

## A sprite drawn on top of the item icon.
class SpritePart:
	var texture_path: String
	var offset: Vector2
	var behind := false

func describe() -> String:
	return "%s (%s)" % [display_name, Rarity.keys()[rarity]]
