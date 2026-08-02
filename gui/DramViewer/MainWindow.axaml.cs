using System;
using System.Collections.Generic;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;

namespace DramViewer;

// 화면에 뿌릴 한 행. Text는 글자, RowColor는 배경(escape면 노랑)
public class Row
{
    public string Text { get; set; } = "";
    public IBrush RowColor { get; set; } = Brushes.Transparent;
    public IBrush TextColor { get; set; } = Brushes.White;
    public string Result { get; set; } = "";
    public bool IsEscape { get; set; }
}

public partial class MainWindow : Window
{
    static readonly string[] Header =
    {
        "test_id", "result", "start_addr", "length", "pattern",
        "words", "errors", "first_fail", "expected", "actual",
        "corr", "uncorr", "note"
    };
    static readonly int[] ColWidth = { 34, 7, 12, 8, 12, 8, 8, 12, 12, 12, 6, 7, 34 };

    readonly List<Row> _rows = new List<Row>();

    public MainWindow()
    {
        InitializeComponent();
        PathBox.Text = Path.GetFullPath("../../dram_test_results.csv");
        DrawHeatmap();
    }

    // [CSV 불러오기] 버튼
    void OnLoadClick(object? sender, RoutedEventArgs e)
    {
        string path = (PathBox.Text ?? "").Trim();
        if (!File.Exists(path))
        {
            StatusText.Text = "파일 없음: " + path;
            return;
        }

        _rows.Clear();
        string[] lines = File.ReadAllLines(path);
        for (int i = 1; i < lines.Length; i++)
        {
            Row? row = ParseLine(lines[i]);
            if (row != null)
            {
                _rows.Add(row);
            }
        }

        HeaderText.Text = Line(Header);
        Redraw();
    }

    // 필터 콤보를 바꾸면 다시 그림
    void OnViewChanged(object? sender, SelectionChangedEventArgs e)
    {
        Redraw();
    }

    // CSV 한 줄 -> Row. 컬럼이 모자라면 무시
    Row? ParseLine(string line)
    {
        string[] f = line.Split(',');
        if (f.Length < 11)
        {
            return null;
        }
        for (int i = 0; i < f.Length; i++)
        {
            f[i] = Clean(f[i]);
        }

        bool escape = f[1] == "PASS" && ToInt(f[10]) > 0; // PASS인데 ECC 정정함
        Row row = new Row();
        row.Text = Line(f);
        row.Result = f[1];
        row.IsEscape = escape;
        row.RowColor = escape ? new SolidColorBrush(Color.FromRgb(255, 236, 179))
                              : Brushes.Transparent;
        row.TextColor = escape ? Brushes.Black : Brushes.White;
        return row;
    }

    // 필터를 적용해 목록을 다시 만든다
    void Redraw()
    {
        // 창이 다 만들어지기 전에 콤보 SelectedIndex="0"이 이 함수를 부를 수 있다
        if (FilterBox == null)
        {
            return;
        }

        int filter = FilterBox.SelectedIndex;
        List<Row> view = new List<Row>();
        int escapes = 0;

        for (int i = 0; i < _rows.Count; i++)
        {
            Row r = _rows[i];
            if (r.IsEscape)
            {
                escapes++;
            }

            bool keep = filter == 0
                || (filter == 1 && r.IsEscape)
                || (filter == 2 && r.Result == "PASS")
                || (filter == 3 && r.Result == "FAIL");
            if (keep)
            {
                view.Add(r);
            }
        }

        RowsList.ItemsSource = view;
        StatusText.Text = view.Count + "/" + _rows.Count + " rows (escape " + escapes + ")";
    }

    // logger.c가 엑셀 자동변환을 피하려고 ="0x.." 로 감싼 걸 벗겨낸다
    static string Clean(string s)
    {
        s = s.Trim();
        if (s.StartsWith("=\"") && s.EndsWith("\""))
        {
            s = s.Substring(2, s.Length - 3);
        }
        return s;
    }

    static int ToInt(string s)
    {
        int v;
        return int.TryParse(s, out v) ? v : 0;
    }

    // 고정폭 폰트 기준으로 열을 맞춘 한 줄
    static string Line(IReadOnlyList<string> f)
    {
        string s = "";
        for (int i = 0; i < ColWidth.Length; i++)
        {
            string v = (i < f.Count) ? f[i] : "";
            s += v.PadRight(ColWidth[i]) + " ";
        }
        return s;
    }

    // ---- 히트맵 ----
    // host의 --test addrmap이 만든 bank_map.csv(뱅크별 linear/bank_hash 카운트)를
    // 읽어 격자를 그린다. 뱅크 계산은 host/addr_map.c가 하고 여기선 그리기만

    // bank_map.csv의 각 뱅크 카운트. [뱅크][0]=linear, [뱅크][1]=bank_hash
    readonly int[,] _bank = new int[32, 2];
    bool _bankLoaded;

    void OnHeatChanged(object? sender, SelectionChangedEventArgs e)
    {
        DrawHeatmap();
    }

    void LoadBankMap()
    {
        _bankLoaded = false;
        string path = Path.GetFullPath("../../bank_map.csv");
        if (!File.Exists(path))
        {
            return;
        }

        string[] lines = File.ReadAllLines(path);
        for (int i = 1; i < lines.Length; i++)
        {
            string[] f = lines[i].Split(',');
            if (f.Length < 3)
            {
                continue;
            }
            int bank = ToInt(f[0]);
            if (bank >= 0 && bank < 32)
            {
                _bank[bank, 0] = ToInt(f[1]);
                _bank[bank, 1] = ToInt(f[2]);
            }
        }
        _bankLoaded = true;
    }

    void DrawHeatmap()
    {
        if (HeatGrid == null)
        {
            return;
        }
        if (!_bankLoaded)
        {
            LoadBankMap();
        }
        if (!_bankLoaded)
        {
            HeatStatus.Text = "bank_map.csv 없음 (host: ./dram_test --test addrmap)";
            return;
        }

        int col = HeatMapBox.SelectedIndex; // 0 linear, 1 bank_hash
        int max = 1;
        int lit = 0;
        for (int i = 0; i < 32; i++)
        {
            if (_bank[i, col] > max) { max = _bank[i, col]; }
            if (_bank[i, col] > 0) { lit++; }
        }

        HeatGrid.Children.Clear();
        for (int i = 0; i < 32; i++)
        {
            HeatGrid.Children.Add(MakeCell(i, _bank[i, col], max));
        }
        HeatStatus.Text = "fail이 닿은 뱅크: " + lit + "/32";
    }

    // 한 뱅크 칸. fail이 많을수록 빨갛게
    static Control MakeCell(int bank, int count, int max)
    {
        byte red = (byte)(40 + count * 200 / max);
        Border cell = new Border();
        cell.Background = (count == 0)
            ? new SolidColorBrush(Color.FromRgb(40, 40, 40))
            : new SolidColorBrush(Color.FromRgb(red, 40, 40));
        cell.BorderBrush = Brushes.Black;
        cell.BorderThickness = new Thickness(1);

        TextBlock label = new TextBlock();
        label.Text = "b" + bank + "\n" + count;
        label.Foreground = Brushes.White;
        label.FontSize = 11;
        label.HorizontalAlignment = HorizontalAlignment.Center;
        label.VerticalAlignment = VerticalAlignment.Center;
        cell.Child = label;
        return cell;
    }
}
