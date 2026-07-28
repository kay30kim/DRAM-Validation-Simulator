using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;

namespace DramViewer;

public class LogRow
{
    public string Display { get; set; } = "";
    public string Result { get; set; } = "";
    public int Errors { get; set; }
    public int Corr { get; set; }
    public bool IsEscape { get; set; }
}

public partial class MainWindow : Window
{
    static readonly string[] kHeader =
    {
        "test_id", "result", "start_addr", "length", "pattern",
        "words", "errors", "first_fail", "expected", "actual",
        "corr", "uncorr", "note"
    };
    static readonly int[] kWidth = { 34, 7, 12, 8, 12, 8, 8, 12, 12, 12, 6, 7, 34 };

    readonly List<LogRow> _all = new List<LogRow>();

    public MainWindow()
    {
        InitializeComponent();
        PathBox.Text = Path.GetFullPath("../../dram_test_results.csv");
    }

    void OnLoadClick(object? sender, RoutedEventArgs e)
    {
        string path = (PathBox.Text == null) ? "" : PathBox.Text.Trim();

        if (path.Length == 0 || !File.Exists(path))
        {
            StatusText.Text = "파일 없음: " + path;
            return;
        }

        try
        {
            string[] lines = File.ReadAllLines(path);
            _all.Clear();

            for (int i = 1; i < lines.Length; i++)
            {
                if (lines[i].IndexOf(',') < 0)
                {
                    continue;
                }

                string[] fields = lines[i].Split(',');
                for (int j = 0; j < fields.Length; j++)
                {
                    fields[j] = Clean(fields[j]);
                }

                LogRow row = new LogRow();
                row.Display = FormatRow(fields);
                row.Result = (fields.Length > 1) ? fields[1] : "";
                row.Errors = (fields.Length > 6) ? ParseInt(fields[6]) : 0;
                row.Corr = (fields.Length > 10) ? ParseInt(fields[10]) : 0;
                // escape = 테스트는 통과인데 ECC가 조용히 정정한 행
                row.IsEscape = (row.Result == "PASS") && (row.Corr > 0);
                _all.Add(row);
            }

            HeaderText.Text = FormatRow(kHeader);
            ApplyView();
        }
        catch (Exception ex)
        {
            StatusText.Text = ex.Message;
        }
    }

    void OnViewChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (_all.Count > 0)
        {
            ApplyView();
        }
    }

    // 필터/정렬을 적용해 화면 목록을 다시 만든다
    void ApplyView()
    {
        int filter = (FilterBox == null) ? 0 : FilterBox.SelectedIndex;
        int sort = (SortBox == null) ? 0 : SortBox.SelectedIndex;

        List<LogRow> view = new List<LogRow>();
        int pass = 0;
        int fail = 0;
        int escapes = 0;

        for (int i = 0; i < _all.Count; i++)
        {
            LogRow r = _all[i];

            if (r.Result == "PASS") { pass++; }
            if (r.Result == "FAIL") { fail++; }
            if (r.IsEscape) { escapes++; }

            bool keep = true;
            if (filter == 1) { keep = r.IsEscape; }
            else if (filter == 2) { keep = (r.Result == "PASS"); }
            else if (filter == 3) { keep = (r.Result == "FAIL"); }

            if (keep)
            {
                view.Add(r);
            }
        }

        if (sort == 1) { view.Sort(CompareByResult); }
        else if (sort == 2) { view.Sort(CompareByErrors); }
        else if (sort == 3) { view.Sort(CompareByCorr); }

        IBrush escapeBg = new SolidColorBrush(Color.FromRgb(255, 236, 179));
        List<Control> items = new List<Control>();
        for (int i = 0; i < view.Count; i++)
        {
            TextBlock tb = new TextBlock();
            tb.Text = view[i].Display;
            tb.FontFamily = new FontFamily("Menlo,Consolas,monospace");
            tb.FontSize = 12;
            if (view[i].IsEscape)
            {
                tb.Background = escapeBg;
                tb.Foreground = Brushes.Black;
            }
            items.Add(tb);
        }

        RowsList.ItemsSource = items;
        StatusText.Text = view.Count + "/" + _all.Count + " rows  (PASS " + pass +
                          " / FAIL " + fail + " / escape " + escapes + ")";
    }

    static int CompareByResult(LogRow a, LogRow b)
    {
        return string.CompareOrdinal(a.Result, b.Result);
    }

    static int CompareByErrors(LogRow a, LogRow b)
    {
        return b.Errors - a.Errors;
    }

    static int CompareByCorr(LogRow a, LogRow b)
    {
        return b.Corr - a.Corr;
    }

    static int ParseInt(string s)
    {
        int v = 0;
        if (int.TryParse(s, out v))
        {
            return v;
        }
        return 0;
    }

    // logger.c가 엑셀 자동변환을 피하려고 ="0x.." 로 감싸므로 벗겨낸다
    static string Clean(string s)
    {
        s = s.Trim();
        if (s.Length > 2 && s.StartsWith("=\"") && s.EndsWith("\""))
        {
            s = s.Substring(2, s.Length - 3);
        }
        return s;
    }

    // 고정폭 폰트 기준으로 열을 맞춰 한 줄 문자열로 만든다
    static string FormatRow(IReadOnlyList<string> fields)
    {
        string line = "";
        for (int i = 0; i < kWidth.Length; i++)
        {
            string value = (i < fields.Count) ? fields[i] : "";
            line += value.PadRight(kWidth[i]) + " ";
        }
        return line;
    }
}
