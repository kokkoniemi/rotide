defmodule Injections do
  # ~r regex sigil content is injected as the regex grammar.
  @pattern ~r/[A-Za-z_][A-Za-z0-9_]*/

  # ~JS sigil content is injected as JavaScript.
  @script ~JS"""
  const total = items.reduce((sum, n) => sum + n, 0);
  console.log(total);
  """

  def pattern, do: @pattern
  def script, do: @script
end
