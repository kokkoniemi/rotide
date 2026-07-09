defmodule Greeter do
  def hello(name) do
    "Hello, " <> name <> "!"
  end
end

IO.puts(Greeter.hello("world"))
