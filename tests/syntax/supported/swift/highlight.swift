/// Highlighting sampler
import Foundation

struct Point {
    let x: Int
    var y: Int

    func distance(to other: Point) -> Double {
        let dx = Double(x - other.x)
        return dx
    }
}

public func greet(name: String) -> String {
    let message = "Hello, \(name)"
    return message
}

let ready = true
