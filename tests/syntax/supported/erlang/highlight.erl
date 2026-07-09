%% Constants and helpers
-module(rotide_highlight).
-export([start/0, classify/1]).
-record(user, {name, active = true}).

-spec start() -> ok.
start() ->
    User = #user{name = <<"Ada">>},
    case classify(42) of
        positive ->
            io:format("~p~n", [User#user.name]),
            ok;
        Other ->
            {error, Other}
    end.

classify(N) when is_integer(N), N > 0 ->
    positive;
classify(0) ->
    zero;
classify(_) ->
    negative.
