-module(rotide_incomplete).

broken(Value) ->
    case Value of
        {ok, N} when N > 0 ->
            io:format("~p", [N]);
        error ->
