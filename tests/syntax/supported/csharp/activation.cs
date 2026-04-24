namespace Hello;

public class Greeter
{
    public string Greet(string name)
    {
        return "hello, " + name;
    }
}

public interface INamed
{
    string Name();
}
