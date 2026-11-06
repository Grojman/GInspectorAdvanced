class_name GrantAttackEffect
extends ItemEffect
## Gives the wearer's attacks another effect (composition: an effect inside an effect).

@export var effect: ItemEffect
@export_range(1, 10) var stacks: int = 1
