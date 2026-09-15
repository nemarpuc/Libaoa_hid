// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// Compares managed layout and constants against the compiled C ABI oracle; it
// does not exercise an Android device.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using AoaHid;

static string SnakeCase(string name)
{
    var value = new StringBuilder();
    for (var index = 0; index < name.Length; ++index)
    {
        var current = name[index];
        if (char.IsUpper(current) && index != 0)
        {
            var previous = name[index - 1];
            var nextIsLower = index + 1 < name.Length && char.IsLower(name[index + 1]);
            if (char.IsLower(previous) || char.IsDigit(previous) || nextIsLower)
                value.Append('_');
        }
        value.Append(char.ToLowerInvariant(current));
    }
    return value.ToString();
}

static Dictionary<string, long> ReadOracle(string executable)
{
    using var process = Process.Start(new ProcessStartInfo(executable)
    {
        RedirectStandardOutput = true,
        UseShellExecute = false,
    }) ?? throw new InvalidOperationException("failed to start C ABI oracle");
    var values = new Dictionary<string, long>();
    while (process.StandardOutput.ReadLine() is { } line)
    {
        var separator = line.IndexOf('=');
        if (separator <= 0)
            throw new InvalidDataException($"invalid oracle line: {line}");
        values.Add(line[..separator], long.Parse(line[(separator + 1)..]));
    }
    process.WaitForExit();
    if (process.ExitCode != 0)
        throw new InvalidOperationException($"C ABI oracle exited {process.ExitCode}");
    return values;
}

var options = new KeyboardOptions();
if (options.StructSize != 0)
    return 1;

var oraclePath = args.Length == 1 ? args[0] : Environment.GetEnvironmentVariable("AOAHID_ABI_ORACLE");
if (string.IsNullOrWhiteSpace(oraclePath))
    throw new InvalidOperationException("pass the compiled C layout oracle path");
var oracle = ReadOracle(oraclePath);
var visited = new HashSet<string>();
var oracleLayoutKeys = new HashSet<string>();
var oracleConstants = new Dictionary<string, long>();
foreach (var pair in oracle)
{
    if (pair.Key.StartsWith("aoahid_", StringComparison.Ordinal))
        oracleLayoutKeys.Add(pair.Key);
    else if (pair.Key.StartsWith("AOAHID_", StringComparison.Ordinal))
        oracleConstants.Add(pair.Key, pair.Value);
    else
        throw new InvalidDataException($"unknown C ABI oracle key: {pair.Key}");
}

foreach (var pair in Native.AbiStructs)
{
    var sizeKey = $"{pair.Key}.size";
    if (Marshal.SizeOf(pair.Value) != oracle[sizeKey])
        throw new InvalidDataException($"size mismatch: {pair.Key}");
    visited.Add(sizeKey);

    foreach (var field in pair.Value.GetFields(BindingFlags.Instance | BindingFlags.Public))
    {
        // SnakeCase() cannot recover the underscore SnakeCase-Options.ScanTimeUnit100us drops
        // before "100us" (no uppercase letter marks that boundary), so this field is special-cased
        // by name rather than by declaring type; both TouchscreenOptions and TouchpadOptions declare it.
        var cField = field.Name == nameof(TouchscreenOptions.ScanTimeUnit100us)
            ? "scan_time_unit_100us"
            : SnakeCase(field.Name);
        var key = $"{pair.Key}.{cField}";
        var actual = Marshal.OffsetOf(pair.Value, field.Name).ToInt64();
        if (actual != oracle[key])
            throw new InvalidDataException($"offset mismatch: {key} (C# {actual}, C {oracle[key]})");
        visited.Add(key);
    }
}

if (!visited.SetEquals(oracleLayoutKeys))
    throw new InvalidDataException("the C oracle and C# binding expose different fields");

var managedConstants = new Dictionary<string, long>();
foreach (var pair in Native.AbiConstants)
    managedConstants.Add(pair.Key, pair.Value);
managedConstants.Add("AOAHID_VERSION_MAJOR", Native.AOAHID_VERSION_MAJOR);
managedConstants.Add("AOAHID_VERSION_MINOR", Native.AOAHID_VERSION_MINOR);
managedConstants.Add("AOAHID_VERSION_PATCH", Native.AOAHID_VERSION_PATCH);

if (!new HashSet<string>(managedConstants.Keys).SetEquals(oracleConstants.Keys))
    throw new InvalidDataException("the C oracle and C# binding expose different constants");
foreach (var pair in managedConstants)
{
    if (pair.Value != oracleConstants[pair.Key])
        throw new InvalidDataException(
            $"constant mismatch: {pair.Key} (C# {pair.Value}, C {oracleConstants[pair.Key]})");
}
return 0;
