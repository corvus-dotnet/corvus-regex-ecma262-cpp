<#
.SYNOPSIS
    Writes docs/api.md, the API reference the documentation site publishes, from the declarations of
    include/corvus/regex_ecma262.hpp and their comments.

.DESCRIPTION
    The header's conventions (its opening comment), then every declaration of namespace corvus::ecma262 in its order,
    with the comment written above it: a function's signature without its body, an enum whole, and a class with each
    of its public members, overloads together. The namespace detail and the private part of each class are left out;
    a type within a class is one block of its public declarations.

.PARAMETER Check
    Compares what would be written with docs/api.md and writes nothing. The script fails when they differ.
#>
param([switch] $Check)

$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$headerPath = Join-Path $root 'include' 'corvus' 'regex_ecma262.hpp'
$out = Join-Path $root 'docs' 'api.md'

$header = [System.IO.File]::ReadAllText($headerPath) -replace "`r", ''
$open = $header.IndexOf("namespace corvus::ecma262 {`n")
$close = $header.IndexOf("}  // namespace corvus::ecma262")
if ($open -lt 0 -or $close -lt 0) { throw "$headerPath has no namespace corvus::ecma262." }
$script:src = $header.Substring(0, $close)

# The index of the brace that closes the one at $at, past strings, characters and comments.
function Skip-Balanced([int]$at) {
    $depth = 0
    for ($i = $at; $i -lt $script:src.Length; $i++) {
        $c = $script:src[$i]
        if ($c -eq '"' -or $c -eq "'") { $i = Skip-Literal $i; continue }
        if ($c -eq '/' -and $script:src[$i + 1] -eq '/') { $i = $script:src.IndexOf("`n", $i); continue }
        if ($c -eq '{') { $depth++ }
        elseif ($c -eq '}') { $depth--; if ($depth -eq 0) { return $i } }
    }
    throw "No brace closes the one at $at."
}

# The index of the quote that closes the string or character literal at $at (a raw string's too).
function Skip-Literal([int]$at) {
    $q = $script:src[$at]
    if ($q -eq '"' -and $at -gt 0 -and $script:src[$at - 1] -eq 'R') {
        $paren = $script:src.IndexOf('(', $at)
        $delimiter = ')' + $script:src.Substring($at + 1, $paren - $at - 1) + '"'
        return $script:src.IndexOf($delimiter, $paren) + $delimiter.Length - 1
    }
    for ($i = $at + 1; $i -lt $script:src.Length; $i++) {
        if ($script:src[$i] -eq '\') { $i++; continue }
        if ($script:src[$i] -eq $q) { return $i }
    }
    throw "No quote closes the one at $at."
}

# Continuation lines of a declaration lose the indentation of the column it starts at.
function Format-Code([string]$text, [int]$column) {
    $lines = $text.TrimEnd() -split "`n"
    $pad = ' ' * $column
    return (@($lines[0]) + @($lines | Select-Object -Skip 1 | ForEach-Object { if ($_.StartsWith($pad)) { $_.Substring($column) } else { $_.TrimStart() } })) -join "`n"
}

function Get-Name([string]$head, [string]$kind) {
    # The last class-key names the type: the first of a template's may be its parameter's.
    if ($kind -eq 'type') { if ($head -match '(?s).*\b(?:class|struct|enum class|enum)\s+(\w+)') { return $Matches[1] } }
    if ($head -match '(?m)^\s*using\s+(\w+)\s*=') { return $Matches[1] }
    if ($kind -eq 'function') {
        $before = $head.Substring(0, $head.IndexOf('('))
        if ($before -match 'operator\s*(\S+)\s*$') {
            $op = $Matches[1]
            return $(if ($op -match '^\w') { "operator $op" } else { "operator$op" })
        }
        if ($before -match '(~?\w+)\s*$') { return $Matches[1] }
    }
    if ($head -match '(\w+)\s*(?:=|\{|;|$)') { return $Matches[1] }
    throw "No name in: $head"
}

# The declarations from $start to $end (the body of the namespace or of a class): each with its name, its kind, its
# code, the comment above it and, for a class, its members. Only the public ones of a class are kept.
function Get-Declarations([int]$start, [int]$end, [bool]$public) {
    $items = [System.Collections.Generic.List[object]]::new()
    $comment = [System.Collections.Generic.List[string]]::new()
    $i = $start
    while ($i -lt $end) {
        $c = $script:src[$i]
        if ($c -eq "`n") {
            # A blank line parts a comment from what follows it.
            if ($i + 1 -lt $end -and $script:src.Substring($i + 1, [Math]::Min(1, $end - $i - 1)) -eq "`n") { $comment.Clear() }
            $i++; continue
        }
        if ([char]::IsWhiteSpace($c)) { $i++; continue }
        $eol = $script:src.IndexOf("`n", $i); if ($eol -lt 0 -or $eol -gt $end) { $eol = $end }
        $line = $script:src.Substring($i, $eol - $i)
        if ($line.StartsWith('//')) { $comment.Add(($line -replace '^// ?', '')); $i = $eol; continue }
        if ($line.StartsWith('#')) { $comment.Clear(); $i = $eol; continue }
        if ($line -match '^(public|private|protected):') { $public = $Matches[1] -eq 'public'; $comment.Clear(); $i = $eol; continue }
        if ($line -match '^namespace\s+\w+\s*\{') { $i = (Skip-Balanced $script:src.IndexOf('{', $i)) + 1; $comment.Clear(); continue }

        $column = $i - $script:src.LastIndexOf("`n", $i) - 1
        $from = $i
        $paren = 0
        $kind = 'variable'
        $code = $null
        $members = $null
        while ($true) {
            $c = $script:src[$i]
            if ($c -eq '"' -or $c -eq "'") { $i = (Skip-Literal $i) + 1; continue }
            if ($c -eq '/' -and $script:src[$i + 1] -eq '/') { $i = $script:src.IndexOf("`n", $i); continue }
            if ($c -eq '(') { $paren++ }
            elseif ($c -eq ')') { $paren-- }
            elseif ($paren -eq 0 -and $c -eq ';') {
                $text = $script:src.Substring($from, $i - $from + 1)
                if ($text -match '\(' -and $text -notmatch '^\s*using\b' -and $text -notmatch '^[^(]*\s=\s') { $kind = 'function' }
                $code = Format-Code $text $column
                $i++
                break
            }
            elseif ($paren -eq 0 -and $c -eq '{') {
                $head = $script:src.Substring($from, $i - $from)
                $previous = $head.TrimEnd()
                if ($previous.EndsWith('=')) { $i = (Skip-Balanced $i) + 1; continue }
                if ($head -match '^(template\s*<[^{]*>\s*)?(class|struct|enum class|enum)\b') {
                    $kind = 'type'
                    $closing = Skip-Balanced $i
                    if ($Matches[2] -like 'enum*') {
                        $code = Format-Code ($script:src.Substring($from, $closing - $from + 1) + ';') $column
                    } else {
                        $members = Get-Declarations ($i + 1) $closing ($Matches[2] -eq 'struct')
                        $code = Format-Code $previous $column
                    }
                    $i = $script:src.IndexOf(';', $closing) + 1
                    break
                }
                if ($head -notmatch '\(' -and $previous -match '\w$') { $i = (Skip-Balanced $i) + 1; continue }
                # A function's body: the signature without it, and without a constructor's initializers.
                $kind = 'function'
                $signature = $previous -replace '(?s)\)\s*:\s*\w+\(.*$', ')'
                $code = (Format-Code $signature $column) + ';'
                $i = (Skip-Balanced $i) + 1
                break
            }
            $i++
        }
        $doc = @($comment)
        $comment.Clear()
        if (-not $public) { continue }
        if ($code -match '^(template\s*<[^>]*>\s*)?friend\s+class\b' -or $code -match '^static_assert\b' -or
            $code -match '^(class|struct)\s+\w+;$') { continue }
        $items.Add([pscustomobject]@{ Name = (Get-Name $code $kind); Kind = $kind; Code = $code; Doc = $doc; Members = $members })
    }
    return $items
}

# A comment as Markdown: its prose escaped, its lines indented four or more spaces kept as code.
function Convert-Comment([string[]]$lines) {
    $text = ($lines | ForEach-Object {
        if ($_ -match '^    ') { $_ } else { $_ -replace '([\\`*<\[\]])', '\$1' }
    }) -join "`n"
    return $text.Trim()
}

# A type within a class: one block of its public declarations, each with its comment.
function Format-Nested($item) {
    if (-not $item.Members) { return $item.Code }
    # A blank line before each declaration with a comment.
    $body = [System.Text.StringBuilder]::new()
    foreach ($m in $item.Members) {
        $block = @($m.Doc | ForEach-Object { "// $_".TrimEnd() }) + @((Format-Nested $m) -split "`n")
        if ($body.Length -and $m.Doc.Count) { [void]$body.Append("`n") }
        [void]$body.Append((($block | ForEach-Object { "    $_".TrimEnd() }) -join "`n")).Append("`n")
    }
    return "$($item.Code) {`n$($body.ToString())};"
}

$sb = [System.Text.StringBuilder]::new()
function Add([string]$line = '') { [void]$sb.Append($line).Append("`n") }

function Add-Entries($items, [string]$level, [string]$prefix) {
    # Overloads, and an alias or a constant with no comment of its own after one of its kind, are one entry.
    $groups = [System.Collections.Generic.List[object]]::new()
    foreach ($item in $items) {
        $last = $null
        if ($groups.Count) { $last = $groups[$groups.Count - 1] }
        if ($last -and ($last[-1].Name -eq $item.Name -or
                ($item.Doc.Count -eq 0 -and $item.Kind -eq 'variable' -and $last[-1].Kind -eq 'variable'))) { $last.Add($item); continue }
        $group = [System.Collections.Generic.List[object]]::new()
        $group.Add($item)
        $groups.Add($group)
    }
    foreach ($group in $groups) {
        $first = $group[0]
        Add
        Add "$level $prefix$($first.Name)"
        Add
        Add '```cpp'
        Add (($group | ForEach-Object { if ($prefix -and $_.Members) { Format-Nested $_ } else { $_.Code } }) -join "`n`n")
        Add '```'
        $docs = @($group | Where-Object { $_.Doc.Count } | ForEach-Object { Convert-Comment $_.Doc })
        if ($docs.Count) { Add; Add ($docs -join "`n`n") }
        if (-not $prefix -and $first.Members) { Add-Entries $first.Members '###' "$($first.Name)::" }
    }
}

$conventions = [regex]::Match($header, '(?ms)^// The conventions:\n(.*?)^//\n// Licensed').Groups[1].Value
if (-not $conventions) { throw "$headerPath has no conventions in its opening comment." }

Add '# API reference'
Add
Add 'The C++ API: the header `corvus/regex_ecma262.hpp`, namespace `corvus::ecma262`, generated from its declarations'
Add 'and their comments by `tools/docs/gen-api.ps1`. [Getting started](getting-started.md) shows it at work.'
Add
Add '## Conventions'
Add
Add (Convert-Comment (($conventions.TrimEnd("`n") -split "`n") | ForEach-Object { $_ -replace '^// ?', '' }))
Add-Entries (Get-Declarations ($open + "namespace corvus::ecma262 {`n".Length) $close $true) '##' ''
$text = $sb.ToString().TrimEnd("`n") + "`n"

if ($Check) {
    $current = if (Test-Path $out) { [System.IO.File]::ReadAllText($out) } else { '' }
    if ($current -ne $text) { throw 'docs/api.md is not what the header makes: run tools/docs/gen-api.ps1.' }
    Write-Host 'docs/api.md is current.'
    return
}
[System.IO.File]::WriteAllText($out, $text, [System.Text.UTF8Encoding]::new($false))
Write-Host 'Wrote docs/api.md.'
