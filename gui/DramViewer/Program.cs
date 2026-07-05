using System;
using Avalonia;

namespace DramViewer;

class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        AppBuilder builder = BuildAvaloniaApp();
        builder.StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp()
    {
        AppBuilder builder = AppBuilder.Configure<App>();
        builder = builder.UsePlatformDetect();
#if DEBUG
        builder = builder.WithDeveloperTools();
#endif
        builder = builder.WithInterFont();
        builder = builder.LogToTrace();
        return builder;
    }
}
