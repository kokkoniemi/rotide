# Highlighting sampler
extends Node

@export var speed: float = 3.14
const MAX_HP := 42

func spawn(count: int) -> bool:
	var label := "hello"
	print(label)
	if count > 0:
		return true
	return false
