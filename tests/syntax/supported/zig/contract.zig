//! Contract fixture exercising a broad slice of Zig syntax.
const std = @import("std");
const builtin = @import("builtin");

const MAX_ENTRIES: usize = 128;
const PI: f64 = 3.14159;

const Direction = enum {
    north,
    south,
    east,
    west,
};

const Vec2 = struct {
    x: f32 = 0.0,
    y: f32 = 0.0,

    pub fn length(self: Vec2) f32 {
        return @sqrt(self.x * self.x + self.y * self.y);
    }

    pub fn add(self: Vec2, other: Vec2) Vec2 {
        return Vec2{ .x = self.x + other.x, .y = self.y + other.y };
    }
};

const ParseError = error{
    Overflow,
    InvalidCharacter,
};

fn parseDigit(c: u8) ParseError!u8 {
    if (c < '0' or c > '9') {
        return ParseError.InvalidCharacter;
    }
    return c - '0';
}

pub fn sumSlice(values: []const i32) i64 {
    var total: i64 = 0;
    for (values) |value| {
        total += value;
    }
    return total;
}

pub fn classify(dir: Direction) []const u8 {
    return switch (dir) {
        .north => "n",
        .south => "s",
        else => "?",
    };
}

pub fn main() !void {
    var origin = Vec2{};
    const shifted = origin.add(Vec2{ .x = 3.0, .y = 4.0 });
    const dist = shifted.length();

    const numbers = [_]i32{ 1, 2, 3 };
    const total = sumSlice(&numbers);

    const digit = parseDigit('7') catch 0;
    std.debug.print("d={d} dist={d} total={d}\n", .{ digit, dist, total });

    comptime {
        const flag = builtin.is_test;
        _ = flag;
    }
}
