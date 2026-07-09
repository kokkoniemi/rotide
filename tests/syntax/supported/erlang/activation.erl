-module(rotide_sample).
-export([greet/1]).

greet(Name) ->
    io:format("Hello, ~s~n", [Name]).
