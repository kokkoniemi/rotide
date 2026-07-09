# Constants and helpers
defmodule Calculator do
  @moduledoc "A tiny calculator module."

  @pi 3.14159

  def square(x) do
    x * x
  end

  def area(radius) do
    @pi * square(radius)
  end

  def classify(n) when is_integer(n) do
    cond do
      n < 0 -> :negative
      n == 0 -> :zero
      true -> :positive
    end
  end

  def shout(name) do
    name
    |> String.upcase()
    |> Kernel.<>("!")
  end
end

flag = true
greeting = "Hello, world"
IO.puts(greeting)
