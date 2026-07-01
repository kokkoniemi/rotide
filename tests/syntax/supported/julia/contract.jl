"""Demo module with a greeting helper."""
module Demo

export greet

const DEFAULT = 2

mutable struct Greeter
    prefix::String
end

function greet(g::Greeter, name::String; times::Int = DEFAULT)
    local message = "$(g.prefix), $name"
    values = [message for _ in 1:times]
    for value in values
        println(value)
    end
    return values
end

square(x) = x ^ 2

macro twice(expr)
    quote
        $expr + $expr
    end
end

end
