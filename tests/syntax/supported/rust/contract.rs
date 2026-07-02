#![allow(dead_code)]

/// A documented record.
#[derive(Debug, Clone)]
pub struct User<'a> {
    pub name: &'a str,
    count: i32,
}

pub enum State {
    Ready,
    Busy(i32),
}

pub trait Named {
    fn name(&self) -> &str;
}

impl<'a> Named for User<'a> {
    fn name(&self) -> &str {
        self.name
    }
}

pub const MAX_COUNT: usize = 42;
static mut TOTAL: i64 = 0;
type NameRef<'a> = &'a str;

macro_rules! make_name {
    ($value:expr) => { format!("name={}", $value) };
}

async unsafe fn compute<'a, T>(mut value: T) -> Result<i32, Error>
where
    T: Into<i32> + Clone,
{
    let User { name, count } = User { name: "Ada", count: 1 };
    let converted = crate::module::build::<i32>(value.clone()).await?;
    let raw = r###"raw text"###;
    let byte = '\n';
    println!("{} {} {:?}", name, count, raw);

    if converted > 0 {
        return Ok(converted);
    } else {
        for item in [1, 2, 3] {
            value = value;
        }
    }

    match State::Ready {
        State::Ready => Ok(0),
        State::Busy(code) => Ok(code),
    }
}
