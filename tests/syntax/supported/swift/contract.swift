/// Contract fixture exercising a broad slice of Swift syntax.
import Foundation

let MAX_COUNT = 100
let pi: Double = 3.14159

enum Direction {
    case north
    case south
    case east
    case west
}

protocol Shape {
    var area: Double { get }
    func describe() -> String
}

struct Circle: Shape {
    let radius: Double

    var area: Double {
        return pi * radius * radius
    }

    func describe() -> String {
        return "Circle(r=\(radius))"
    }
}

class Counter {
    private var value: Int = 0

    func increment(by amount: Int = 1) {
        value += amount
    }

    var current: Int {
        return value
    }
}

enum ParseError: Error {
    case overflow
    case invalid(Character)
}

func parseDigit(_ c: Character) throws -> Int {
    guard let digit = c.wholeNumberValue else {
        throw ParseError.invalid(c)
    }
    return digit
}

func classify(_ dir: Direction) -> String {
    switch dir {
    case .north, .south:
        return "vertical"
    default:
        return "horizontal"
    }
}

func sum(_ values: [Int]) -> Int {
    var total = 0
    for value in values {
        total += value
    }
    return total
}

extension Circle {
    static func unit() -> Circle {
        return Circle(radius: 1.0)
    }
}

func main() async {
    let shapes: [Shape] = [Circle(radius: 2.0)]
    let numbers = [1, 2, 3, 4, 5]
    let doubled = numbers.map { $0 * 2 }

    let counter = Counter()
    counter.increment(by: 5)

    let digit = try? parseDigit("7")
    if let d = digit {
        print("digit=\(d) total=\(sum(doubled)) area=\(shapes[0].area)")
    }
}
