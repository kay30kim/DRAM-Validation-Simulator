using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;
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
}
