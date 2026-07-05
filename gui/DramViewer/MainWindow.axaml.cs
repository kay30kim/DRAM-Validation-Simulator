using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace DramViewer;

public partial class MainWindow : Window
{
    static readonly string[] kHeader =
    {
        "test_name", "status", "start_addr", "length", "pattern",
        "words", "errors", "first_fail", "expected", "actual"
    };
    static readonly int[] kWidth = { 34, 7, 12, 8, 12, 8, 8, 12, 12, 12 };

    public MainWindow()
    {
        InitializeComponent(); // MainWindow.axaml에 선언된 컨트롤들을 실제로 생성
        PathBox.Text = Path.GetFullPath("../../dram_test_results.csv");
    }

    // XAML의 Click="OnLoadClick"과 연결되는 핸들러 (콜백 함수)
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
            List<string> rows = new List<string>();
            int pass = 0;
            int fail = 0;

            // lines[0]은 헤더 줄이므로 1부터
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

                if (fields.Length > 1 && fields[1] == "PASS")
                {
                    pass++;
                }
                if (fields.Length > 1 && fields[1] == "FAIL")
                {
                    fail++;
                }

                rows.Add(FormatRow(fields));
            }

            HeaderText.Text = FormatRow(kHeader);
            RowsList.ItemsSource = rows;
            StatusText.Text = rows.Count + " rows (PASS " + pass + " / FAIL " + fail + ")";
        }
        catch (Exception ex)
        {
            StatusText.Text = ex.Message;
        }
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
