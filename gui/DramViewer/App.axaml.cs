using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;

namespace DramViewer;

public partial class App : Application
{
    // App.axaml(테마 등 전역 설정)을 로드
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    // 프레임워크 초기화가 끝난 시점에 메인 창 생성
    public override void OnFrameworkInitializationCompleted()
    {
        // Avalonia는 모바일/웹 타깃도 있어서, 데스크톱 모드인지 확인 후 창을 만든다
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new MainWindow();
        }

        base.OnFrameworkInitializationCompleted();
    }
}
