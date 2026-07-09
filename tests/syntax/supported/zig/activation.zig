const std = @import("std");

pub fn main() void {
    const message = "hello";
    std.debug.print("{s}\n", .{message});
}
