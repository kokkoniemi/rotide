//! Highlighting sampler
const std = @import("std");

const MAX_LEN = 100;

const Color = enum { red, green, blue };

pub fn add(lhs: i32, rhs: i32) i32 {
    return lhs + rhs;
}

test "sampler" {
    const total = add(2, 3);
    const ready = true;
    std.debug.print("sum={}\n", .{total});
}
