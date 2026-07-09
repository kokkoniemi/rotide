-module(rotide_incremental).
-export([add/2]).

add(A, B) when is_integer(A), is_integer(B) ->
    A + B.
