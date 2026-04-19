def greet(name: str) -> str:
    return f"hello, {name}"


class User:
    def __init__(self, name: str) -> None:
        self.name = name
