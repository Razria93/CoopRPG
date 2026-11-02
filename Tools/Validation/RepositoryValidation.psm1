# P1 repository checks: no engine, network, hooks, or LFS downloads.
Set-StrictMode -Version Latest

function Invoke-RepositoryGit {
    param([string]$Root, [string[]]$Arguments, [int[]]$AllowedCodes = @(0))
    $lines = @(& git --no-optional-locks -C $Root @Arguments 2>&1)
    $code = $LASTEXITCODE
    if ($code -notin $AllowedCodes) { throw "Git $($Arguments[0]) failed (exit $code)." }
    [pscustomobject]@{ Text = ($lines -join "`n"); ExitCode = $code }
}
function Test-WithinRoot {
    param([string]$Root, [string]$Path)
    $Path.StartsWith($Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}
function Test-ReparsePath {
    param([string]$Root, [string]$Path)
    $cursor = $Path
    while ($cursor.Length -ge $Root.Length) {
        if ((Test-Path -LiteralPath $cursor) -and
            (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0)) { return $true }
        if ($cursor -eq $Root) { break }
        $cursor = Split-Path -Parent $cursor
    }
    return $false
}
function Get-MarkdownBody {
    param([string]$Text)
    $fenceCharacter = ''; $fenceLength = 0
    $lines = foreach ($line in ($Text -split "\r?\n")) {
        if ($line -match '^\s{0,3}(`{3,}|~{3,})') {
            $token = $Matches[1]
            if (-not $fenceCharacter) { $fenceCharacter = $token.Substring(0, 1); $fenceLength = $token.Length }
            elseif ($token.StartsWith($fenceCharacter) -and $token.Length -ge $fenceLength -and
                    $line -match '^\s{0,3}(`+|~+)\s*$') { $fenceCharacter = '' }
            ''
        } elseif (-not $fenceCharacter) { $line } else { '' }
    }
    $lines -join "`n"
}
function Get-MarkdownAnchors {
    param([string]$Body)
    $ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($heading in [regex]::Matches($Body, '(?m)^ {0,3}#{1,6}\s+(.+)$')) {
        $name = [regex]::Replace($heading.Groups[1].Value.Trim(), '\s+#+\s*$', '').Replace('`', '').ToLowerInvariant()
        $slug = [regex]::Replace([regex]::Replace($name, '[^\p{L}\p{N}\p{M}_\-\s]', ''), '\s', '-')
        $id = $slug; $suffix = 0
        while (-not $ids.Add($id)) { $suffix++; $id = "$slug-$suffix" }
    }
    # Parse quoted attributes so data-id and text inside title values cannot invent anchors.
    foreach ($tag in [regex]::Matches($Body, '<a\b(?:"[^"]*"|''[^'']*''|[^''">])*>', 'IgnoreCase')) {
        foreach ($attribute in [regex]::Matches($tag.Value, '\s+([\w:-]+)\s*=\s*(?:"([^"]*)"|''([^'']*)'')')) {
            if ($attribute.Groups[1].Value -in @('id', 'name')) {
                $value = if ($attribute.Groups[2].Success) { $attribute.Groups[2].Value } else { $attribute.Groups[3].Value }
                [void]$ids.Add([Net.WebUtility]::HtmlDecode($value))
            }
        }
    }
    return ,$ids
}
function New-RepositoryCheck {
    param([string]$Id, [int]$Items, [string[]]$Failures, [string]$Note = '')
    $issues = @($Failures | Where-Object { $null -ne $_ })
    [pscustomobject]@{
        id = $Id; status = $(if ($issues.Count) { 'failed' } elseif ($Items -eq 0) { 'notApplicable' } else { 'passed' })
        items = $Items; failures = $issues; note = $Note
    }
}
function Invoke-RepositoryValidation {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$RepositoryRoot, [string]$DiffBase = '')
    $started = [DateTimeOffset]::UtcNow
    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $top = (Invoke-RepositoryGit $root @('rev-parse', '--show-toplevel')).Text
    if ([IO.Path]::GetFullPath($top).TrimEnd('\', '/') -ne $root) { throw 'RepositoryRoot must be the Git repository root.' }
    if (Test-ReparsePath $root $root) { throw 'Reparse repository roots are unsupported.' }
    $head = (Invoke-RepositoryGit $root @('rev-parse', '--verify', 'HEAD')).Text
    if ($DiffBase) {
        if ($DiffBase -cnotmatch '^[0-9a-f]{40}$') { throw 'DiffBase must be a full lowercase commit SHA.' }
        $resolvedBase = (Invoke-RepositoryGit $root @('rev-parse', '--verify', ($DiffBase + '^{commit}'))).Text
        if ($resolvedBase -cne $DiffBase) { throw 'DiffBase commit identity mismatch.' }
        [void](Invoke-RepositoryGit $root @('merge-base', '--is-ancestor', $DiffBase, $head))
    }
    $status = (Invoke-RepositoryGit $root @('status', '--porcelain=v1', '--untracked-files=all')).Text
    $branch = (Invoke-RepositoryGit $root @('branch', '--show-current')).Text
    $inventory = (Invoke-RepositoryGit $root @('ls-files', '--cached', '--others', '--exclude-standard', '-z')).Text
    $paths = @($inventory.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries) | Sort-Object -Unique -CaseSensitive)
    $names = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $safe = [Collections.Generic.List[string]]::new()
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($rel in $paths) {
        $full = [IO.Path]::GetFullPath((Join-Path $root $rel))
        if (-not (Test-WithinRoot $root $full) -or (Test-ReparsePath $root $full)) {
            $failures.Add("Unsupported or reparse path: $rel"); continue
        }
        if (-not [IO.File]::Exists($full)) { $failures.Add("Missing or non-file inventory entry: $rel"); continue }
        if ($names.ContainsKey($rel) -and $names[$rel] -cne $rel) { $failures.Add("Case collision: $rel"); continue }
        $names[$rel] = $rel
        if ($rel -match '(?i)(?:^|/)\.vs/' -or
            $rel -match '(?i)^(?:\.vs|\.aws|\.codex|Engine|UnrealEngine|Artifacts|Docs/X|DerivedDataCache)/' -or
            $rel -match '(?i)(?:^|/)\.env(?:\..*)?$' -or
            $rel -match '(?i)^Game/(?:Binaries|Intermediate|Saved|DerivedDataCache)/' -or
            $rel -match '(?i)^Game/[^/]+\.(?:slnx?|vcxproj(?:\.filters)?)$' -or
            $rel -match '(?i)^Game/Plugins/.*/(?:Intermediate|Saved|DerivedDataCache)/' -or
            $rel -match '(?i)(?:^|/)[^/]+\.(?:suo|VC\.db|VC\.opendb|vcxproj\.user|tmp|temp)$') {
            $failures.Add("Forbidden repository input: $rel"); continue
        }
        if (([IO.FileInfo]$full).Length -gt 50MB -and $rel -notmatch '(?i)\.(uasset|umap)$') {
            $failures.Add("Non-LFS input exceeds project budget of 50 MiB: $rel"); continue
        }
        $safe.Add($rel)
    }
    foreach ($required in @('.gitignore', '.gitattributes', 'README.md')) {
        if (-not $names.ContainsKey($required)) { $failures.Add("Required input missing: $required") }
    }
    $checks = [Collections.Generic.List[object]]::new()
    $checks.Add((New-RepositoryCheck 'repository.inputs' $paths.Count @($failures)))
    $ignored = @('.vs/cache.bin', 'Game/Binaries/a.dll', 'Game/Intermediate/a.obj', 'Game/Saved/a.log',
        'Game/DerivedDataCache/a.bin', 'DerivedDataCache/a.bin', 'Game/CoopRPG.sln', 'Game/CoopRPG.slnx',
        'Game/CoopRPG.vcxproj', 'Game/CoopRPG.vcxproj.filters', 'Game/Plugins/Test/Intermediate/a.obj',
        'Game/Plugins/Test/Saved/a.log', 'Game/Plugins/Test/DerivedDataCache/a.bin', 'Artifacts/a.zip',
        'Docs/X/a.md', 'Engine/a.cpp', 'UnrealEngine/a.cpp', '.codex/config.toml', '.aws/credentials',
        '.env', '.env.local', 'scratch.tmp', 'editor.suo')
    $kept = @('Game/CoopRPG.uproject', 'Game/Config/DefaultEngine.ini', 'Game/Source/Test.cpp',
        'Game/Content/Test.uasset', 'Game/Content/Test.umap', 'Game/Plugins/Test/Source/Test.cpp',
        'Game/Plugins/Test/Binaries/Win64/Required.dll', 'Game/Plugins/Test/Build/Test.cs',
        'Tools/Vendor/Original.vcxproj', 'Docs/Validation/Index.md', 'Tools/Validation/Validate-Repository.ps1')
    $failures.Clear()
    foreach ($rel in $ignored + $kept) {
        $ignoredResult = (Invoke-RepositoryGit $root @('check-ignore', '--no-index', '--quiet', '--', $rel) @(0, 1)).ExitCode -eq 0
        if (($rel -in $ignored) -ne $ignoredResult) { $failures.Add("Unexpected ignore policy: $rel") }
    }
    $checks.Add((New-RepositoryCheck 'git.ignore-policy' ($ignored.Count + $kept.Count) @($failures)))
    $failures.Clear()
    $attributePaths = @(@('Game/Content/Test.uasset', 'Game/Content/Test.umap') + @($safe | Where-Object { $_ -match '(?i)\.(uasset|umap)$' }) | Sort-Object -Unique -CaseSensitive)
    foreach ($rel in $attributePaths) {
        $attrs = (Invoke-RepositoryGit $root @('check-attr', '-z', 'filter', 'diff', 'merge', 'text', '--', $rel)).Text.Split([char]0)
        $expected = @{ filter = 'lfs'; diff = 'lfs'; merge = 'lfs'; text = 'unset' }
        for ($i = 0; $i -lt 12; $i += 3) {
            if ($attrs.Count -le ($i + 2) -or $attrs[$i + 2] -ne $expected[$attrs[$i + 1]]) {
                $failures.Add("Unexpected LFS attribute: $rel"); break
            }
        }
    }
    $checks.Add((New-RepositoryCheck 'git.lfs-attributes' ($attributePaths.Count * 4) @($failures) 'Attributes only; no LFS object download or installation.'))
    $failures.Clear(); $linkCount = 0; $documentCount = 0
    $bodies = @{}; $anchors = @{}
    foreach ($rel in @($safe | Where-Object { $_ -match '(?i)\.md$' })) {
        $documentCount++
        $bodies[$rel] = Get-MarkdownBody ([IO.File]::ReadAllText((Join-Path $root $rel)))
        $anchors[$rel] = Get-MarkdownAnchors $bodies[$rel]
    }
    foreach ($rel in $bodies.Keys) {
        $body = [regex]::Replace($bodies[$rel], '(`+)[^\r\n]*?\1', '')
        $links = [Collections.Generic.List[string]]::new()
        foreach ($m in [regex]::Matches($body, '!?\[[^\]\r\n]*\]\(\s*(<[^>\r\n]+>|[^\s)]+)(?:\s+"[^"]*")?\s*\)')) {
            $links.Add($m.Groups[1].Value.Trim('<', '>'))
        }
        $references = @{}
        foreach ($m in [regex]::Matches($body, '(?m)^ {0,3}\[([^\]]+)\]:\s*(<[^>\r\n]+>|[^\s]+)')) {
            $references[$m.Groups[1].Value.Trim().ToLowerInvariant()] = $m.Groups[2].Value.Trim('<', '>')
        }
        foreach ($m in [regex]::Matches($body, '!?\[([^\]\r\n]+)\]\[([^\]\r\n]*)\]')) {
            $key = $(if ($m.Groups[2].Value) { $m.Groups[2].Value } else { $m.Groups[1].Value }).Trim().ToLowerInvariant()
            if ($references.ContainsKey($key)) { $links.Add($references[$key]) }
            else { $failures.Add("Undefined reference in $($rel): $key") }
        }
        foreach ($url in $links) {
            if ($url -match '^(?:https?|mailto):' -or $url.StartsWith('//')) { continue }
            $linkCount++
            if ($url -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $url -match '^[\\/]') {
                $failures.Add("Non-portable link in $($rel): $url"); continue
            }
            $parts = $url.Split('#', 2)
            try {
                $local = [uri]::UnescapeDataString($parts[0].Split('?', 2)[0])
                $target = if ($local) { [IO.Path]::GetFullPath((Join-Path (Split-Path (Join-Path $root $rel)) $local)) } else { Join-Path $root $rel }
                if (-not (Test-WithinRoot $root $target)) { throw 'outside repository' }
                $targetRel = [IO.Path]::GetRelativePath($root, $target).Replace('\', '/')
                if (-not $names.ContainsKey($targetRel)) { throw 'not a repository input' }
                if ($names[$targetRel] -cne $targetRel) { throw 'path case mismatch' }
                if ($targetRel -notin $safe) { throw 'unsafe or missing input' }
                if ($parts.Length -eq 2 -and $parts[1]) {
                    if (-not $anchors.ContainsKey($targetRel)) { throw 'fragment on unsupported document type' }
                    if (-not $anchors[$targetRel].Contains([uri]::UnescapeDataString($parts[1]))) { throw 'missing anchor' }
                }
            } catch { $failures.Add("Broken link in $($rel): $url ($($_.Exception.Message))") }
        }
    }
    if ($documentCount -eq 0) { $failures.Add('No Markdown documents were checked.') }
    $checks.Add((New-RepositoryCheck 'documents.links' $documentCount @($failures) "$linkCount local links; external URLs are not fetched."))
    $failures.Clear(); $syntaxCount = 0
    foreach ($rel in $safe) {
        $path = Join-Path $root $rel
        if ($rel -match '(?i)\.(json|uproject|uplugin)$') {
            $syntaxCount++
            try { $parsed = [System.Text.Json.JsonDocument]::Parse([IO.File]::ReadAllText($path)); $parsed.Dispose() }
            catch { $failures.Add("Invalid JSON: $rel") }
        } elseif ($rel -match '(?i)\.(ps1|psm1|psd1)$') {
            $syntaxCount++; $tokens = $null; $errors = $null
            [void][Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
            if ($errors.Count) { $failures.Add("Invalid PowerShell syntax: $rel") }
        }
    }
    $checks.Add((New-RepositoryCheck 'configuration.syntax' $syntaxCount @($failures) 'JSON/uproject/uplugin and PowerShell parsing only; not semantic UE validation.'))
    $diff = Invoke-RepositoryGit $root @('diff', '--check', 'HEAD', '--') @(0, 2)
    $diffFailures = if ($diff.ExitCode -ne 0) { @('Tracked diff contains whitespace errors or conflict markers.') } else { @() }
    $diffItems = 1
    if ($DiffBase) {
        $committedDiff = Invoke-RepositoryGit $root @('diff', '--check', $DiffBase, $head, '--') @(0, 2)
        $diffItems++
        if ($committedDiff.ExitCode -ne 0) { $diffFailures += 'Committed diff contains whitespace errors or conflict markers.' }
    }
    $checks.Add((New-RepositoryCheck 'git.diff' $diffItems $diffFailures 'Working tree plus explicit DiffBase-to-HEAD when supplied; no implicit empty-diff fallback.'))
    if ($checks.Count -ne 6) { throw 'Incomplete validation check set.' }
    $failed = @($checks | Where-Object status -eq 'failed').Count
    [pscustomobject]@{
        schemaVersion = 1; validatorVersion = '1.0.2'
        runId = $started.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
        startedAt = $started.ToString('o'); finishedAt = [DateTimeOffset]::UtcNow.ToString('o')
        scope = 'repository-only'; repositoryRoot = $root; head = $head; branch = $branch
        diffBaseSha = $DiffBase; diffMode = $(if ($DiffBase) { 'committed-and-working-tree' } else { 'working-tree-only' })
        workingTreeDirty = [bool]$status; powerShellVersion = $PSVersionTable.PSVersion.ToString()
        validatorSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
        inventoryCount = $paths.Count; plannedChecks = 6; executedChecks = $checks.Count
        failedChecks = $failed; status = $(if ($failed) { 'failed' } else { 'passed' }); checks = $checks.ToArray()
    }
}
Export-ModuleMember -Function Invoke-RepositoryValidation
