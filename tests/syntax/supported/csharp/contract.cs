using System;

namespace Demo.Core;

[Obsolete]
public record Person(string Name, int Age);

interface IRunner
{
    int Run<T>(T value) where T : notnull;
}

enum State
{
    Ready,
    Done
}

class Worker : IRunner
{
    private readonly string name = "worker";

    public Worker(string value)
    {
        name = value;
    }

    public int Run<T>(T value) where T : notnull
    {
        Console.WriteLine($"Value {value}");
        return value is int number ? number : 0;
    }
}
