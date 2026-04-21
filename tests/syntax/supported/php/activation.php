<?php

function greet(string $name): string {
    return "hello, {$name}";
}

class User {
    public string $name;

    public function __construct(string $name) {
        $this->name = $name;
    }
}
