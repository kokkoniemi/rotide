fn greet(name: &str) -> String {
    format!("hello, {}", name)
}

struct User {
    name: String,
}

impl User {
    fn new(name: String) -> Self {
        Self { name }
    }
}
