const std = @import("std");

pub fn main() void {
    var count: i32 = 0;
    while (count < 10) : (count += 1) {
        std.debug.print("{}\n", .{count});
    }
}
