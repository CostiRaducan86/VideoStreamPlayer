using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace VilsSharpX;

/// <summary>
/// Parses and writes Basler Pylon .pfs (GenApi persistence) files.
/// Format: tab-separated lines: ParameterName[\t{Selector=Value}]\tValue
/// Lines starting with '#' are comments.
/// </summary>
internal sealed class PfsConfigParser
{
    public sealed class PfsEntry
    {
        public string Name { get; set; } = "";
        public string? Selector { get; set; }
        public string Value { get; set; } = "";

        /// <summary>Composite key for dictionary lookup: "Name" or "Name{Selector}".</summary>
        public string Key => Selector != null ? $"{Name}{Selector}" : Name;
    }

    private readonly List<string> _commentLines = [];
    private readonly List<PfsEntry> _entries = [];

    public IReadOnlyList<PfsEntry> Entries => _entries;

    public static PfsConfigParser Load(string path)
    {
        var parser = new PfsConfigParser();
        foreach (var line in File.ReadLines(path))
        {
            if (string.IsNullOrWhiteSpace(line)) continue;
            if (line.StartsWith('#'))
            {
                parser._commentLines.Add(line);
                continue;
            }

            var parts = line.Split('\t');
            if (parts.Length == 2)
            {
                parser._entries.Add(new PfsEntry { Name = parts[0], Value = parts[1] });
            }
            else if (parts.Length == 3)
            {
                parser._entries.Add(new PfsEntry { Name = parts[0], Selector = parts[1], Value = parts[2] });
            }
        }
        return parser;
    }

    /// <summary>Gets the value for a simple (no selector) parameter, or null.</summary>
    public string? Get(string name)
    {
        return _entries.FirstOrDefault(e => e.Name == name && e.Selector == null)?.Value;
    }

    /// <summary>Gets the value for a parameter with a specific selector, or null.</summary>
    public string? Get(string name, string selector)
    {
        return _entries.FirstOrDefault(e => e.Name == name && e.Selector == selector)?.Value;
    }

    /// <summary>Sets or adds a simple parameter.</summary>
    public void Set(string name, string value)
    {
        var entry = _entries.FirstOrDefault(e => e.Name == name && e.Selector == null);
        if (entry != null) entry.Value = value;
        else _entries.Add(new PfsEntry { Name = name, Value = value });
    }

    /// <summary>Sets or adds a parameter with selector.</summary>
    public void Set(string name, string selector, string value)
    {
        var entry = _entries.FirstOrDefault(e => e.Name == name && e.Selector == selector);
        if (entry != null) entry.Value = value;
        else _entries.Add(new PfsEntry { Name = name, Selector = selector, Value = value });
    }

    /// <summary>Writes the .pfs back to a file, preserving comment lines at the top.</summary>
    public void Save(string path)
    {
        using var writer = new StreamWriter(path);
        foreach (var c in _commentLines)
            writer.WriteLine(c);
        foreach (var e in _entries)
        {
            if (e.Selector != null)
                writer.WriteLine($"{e.Name}\t{e.Selector}\t{e.Value}");
            else
                writer.WriteLine($"{e.Name}\t{e.Value}");
        }
    }

    /// <summary>
    /// Returns a dictionary of simple parameter names to values
    /// (excludes selector-qualified duplicates).
    /// </summary>
    public Dictionary<string, string> ToSimpleDictionary()
    {
        var dict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in _entries)
        {
            if (e.Selector == null && !dict.ContainsKey(e.Name))
                dict[e.Name] = e.Value;
        }
        return dict;
    }
}
