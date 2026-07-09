# Contract fixture exercising a broad slice of GDScript syntax.
@tool
class_name Enemy
extends CharacterBody2D

signal health_changed(old_value: int, new_value: int)

enum State { IDLE, WALK, ATTACK }

const MAX_HEALTH := 100
const NAMES := ["goblin", "orc", "troll"]

@export var speed: float = 240.0
@export_range(0, 10) var damage: int = 3
@onready var sprite: Sprite2D = $Sprite2D

var _health: int = MAX_HEALTH
var state: State = State.IDLE
var target_path := ^"../Player"
var label_name := &"enemy"

var health: int:
	get:
		return _health
	set(value):
		_health = clampi(value, 0, MAX_HEALTH)
		health_changed.emit(_health, value)


func _ready() -> void:
	set_process(true)
	print("spawned ", name)


func take_damage(amount: int, crit := false) -> void:
	var total := amount
	if crit:
		total = amount * 2
	elif amount < 0:
		total = 0
	else:
		total = amount
	health -= total
	if health <= 0:
		die()


func _process(delta: float) -> void:
	match state:
		State.IDLE:
			velocity = Vector2.ZERO
		State.WALK:
			velocity.x = speed * delta
		_:
			pass
	move_and_slide()


func nearby_names() -> Array:
	var result := []
	for enemy_name in NAMES:
		result.append(enemy_name.to_upper())
	return result


func die() -> void:
	await get_tree().create_timer(1.0).timeout
	queue_free()


static func make_squad(size: int) -> Dictionary:
	var squad := {"count": size, "ready": true}
	var build := func(index): return index + 1
	return squad
