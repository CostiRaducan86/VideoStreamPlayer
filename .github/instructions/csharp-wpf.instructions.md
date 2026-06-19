---
name: CSharp WPF rules
description: Rules for VilsSharpX C# and WPF code.
applyTo: "**/*.{cs,xaml}"
---

# C# / WPF rules

- Keep UI work on the UI thread and acquisition/network work off the UI thread.
- Use `Dispatcher.Invoke` or `Dispatcher.BeginInvoke` before touching WPF controls from callbacks.
- Prefer `async` / `await` and `CancellationToken` for long-running operations.
- Do not silently swallow exceptions.
- Keep protocol parsing separate from rendering.
- Keep protocol constants centralized and named.
- Prefer small, testable methods over broad rewrites.
- If modifying C# source, run or suggest `dotnet build` with a generous timeout.
