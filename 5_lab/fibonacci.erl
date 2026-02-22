#!/usr/bin/env escript

main([A]) ->
    I = list_to_integer(A),
    F = fibonacci(I),
    io:format("fibonacci ~w = ~w~n",[I, F]).

fibonacci(0) -> 0;

fibonacci(1) -> 1;

fibonacci(2) -> 1;

fibonacci(N) -> fibonacci(N-1) + fibonacci(N-2).