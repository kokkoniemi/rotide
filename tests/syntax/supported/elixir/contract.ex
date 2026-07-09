# Comprehensive Elixir fixture exercising the highlight query.
defmodule Store do
  @moduledoc """
  A small in-memory key/value store used to exercise highlighting.
  """

  @default_ttl 3_600
  @version "1.2.3"

  defstruct name: nil, entries: %{}, ttl: @default_ttl

  @type t :: %__MODULE__{name: atom(), entries: map(), ttl: integer()}

  @doc "Create a new store."
  def new(name) when is_atom(name) do
    %Store{name: name, entries: %{}}
  end

  def put(%Store{entries: entries} = store, key, value) do
    %{store | entries: Map.put(entries, key, value)}
  end

  def get(%Store{entries: entries}, key, default \\ nil) do
    Map.get(entries, key, default)
  end

  def keys(%Store{entries: entries}) do
    entries
    |> Map.keys()
    |> Enum.sort()
  end

  def describe(store) do
    count = store.entries |> map_size()

    cond do
      count == 0 -> "empty"
      count < 10 -> "small (#{count})"
      true -> "large"
    end
  end

  def classify(n) when is_number(n) do
    case n do
      0 -> :zero
      x when x < 0 -> :negative
      _ -> :positive
    end
  end

  defp digest(list) when is_list(list) do
    list
    |> Enum.map(fn x -> x * x end)
    |> Enum.filter(&(&1 > 0))
    |> Enum.reduce(0, &+/2)
  end

  def report do
    numbers = [1, 2, 3, 5, 8, 13]
    ratio = 3.5
    flags = {true, false, nil}
    name = :store
    charlist = ~c"raw chars"
    pattern = ~r/[a-z]+/

    IO.puts("version=#{@version} sum=#{digest(numbers)} ratio=#{ratio}")
    {name, flags, charlist, pattern}
  end
end
