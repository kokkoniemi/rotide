extends Node2D

var count := 0

func _process(delta: float) -> void:
	count += 1
	if count > 10:
		count = 0
