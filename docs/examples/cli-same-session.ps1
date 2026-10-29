param(
    [string]$BinDirectory = (Join-Path $PSScriptRoot '../..'),
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('Csrio CLI ' + [Guid]::NewGuid().ToString('N'))),
    [string]$CliExecutable = '',
    [string]$GuiExecutable = '',
    [switch]$CloseWindow
)
$ErrorActionPreference = 'Stop'
if (!$CliExecutable) { $CliExecutable = Join-Path $BinDirectory 'regmapc.exe' }
if (!$GuiExecutable) { $GuiExecutable = Join-Path $BinDirectory 'Csrio.exe' }
$CliExecutable = (Resolve-Path -LiteralPath $CliExecutable).Path
$GuiExecutable = (Resolve-Path -LiteralPath $GuiExecutable).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$project = Join-Path $OutputDirectory 'device.regmap.yaml'
$trace = Join-Path $OutputDirectory 'cli-responses.jsonl'
if (Test-Path -LiteralPath $project) { throw 'The example requires a new project path.' }

function Invoke-Cli([string[]]$Arguments, [int]$ExpectedExit = 0) {
    $start = [Diagnostics.ProcessStartInfo]::new($CliExecutable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $start.Arguments = ((@('--json') + $Arguments) | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' '
    $process = [Diagnostics.Process]::Start($start)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(30000)) { $process.Kill(); throw 'CLI exceeded 30 seconds.' }
    $response = $out.Result | ConvertFrom-Json
    [IO.File]::AppendAllText($trace,($out.Result.Trim()+[Environment]::NewLine),$utf8)
    if ($ExpectedExit -ge 0 -and $process.ExitCode -ne $ExpectedExit) {
        throw ('CLI failed: ' + $out.Result + $err.Result)
    }
    return $response
}
function Save-Patch([string]$Name, [object[]]$Operations) {
    $path = Join-Path $OutputDirectory $Name
    [IO.File]::WriteAllText($path,(@{api_version=1;operations=$Operations} | ConvertTo-Json -Depth 20),$utf8)
    return $path
}
function Assert([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }

$created = Invoke-Cli @('init',$project,'--workspace-id','workspace-main','--name','Channels','--no-generate')
$table = Save-Patch 'create-table.json' @(
    @{op='add';kind='page';id='page-main';parent_id='workspace-main';value=@{name='Main'}},
    @{op='add';kind='block';id='block-ch0';parent_id='page-main';value=@{name='Channel 0';base='auto';size='0x100'}},
    @{op='add';kind='register';id='reg-control';parent_id='block-ch0';value=@{name='CONTROL';offset='auto';type='field';reset='0x1'}},
    @{op='add';kind='field';id='field-mode';parent_id='reg-control';value=@{name='MODE';lsb=0;width=2;type='enum'}},
    @{op='add';kind='enum';id='enum-idle';parent_id='field-mode';value=@{name='IDLE';value=0}},
    @{op='add';kind='enum';id='enum-run';parent_id='field-mode';value=@{name='RUN';value=1}}
)
$null = Invoke-Cli @('apply',$project,$table,'--expect',$created.revision,'--dry-run')
$null = Invoke-Cli @('apply',$project,$table,'--expect',$created.revision)

$start = [Diagnostics.ProcessStartInfo]::new($GuiExecutable)
$start.UseShellExecute = $false
$start.Arguments = '--no-suite-runtime --profile-dir "' + (Join-Path $OutputDirectory 'profile') + '" --project "' + $project + '"'
$window = [Diagnostics.Process]::Start($start)
$session = [string]$window.Id
$deadline = [DateTime]::UtcNow.AddSeconds(30)
do {
    $ready = Invoke-Cli @('gui',$project,'--session',$session) -1
    if ($ready.ok) { break }
    if ($window.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'GUI session did not open this project.' }
    Start-Sleep -Milliseconds 100
} while ($true)
# Opening a project may synchronize RTL; always read the resulting disk revision.
$before = (Invoke-Cli @('summary',$project)).revision
$changes = Save-Patch 'copy-and-edit.json' @(
    @{op='copy';id='block-ch0';new_id='block-ch1';parent_id='page-main';value=@{name='Channel 1';base='auto'}},
    @{op='set';id='block-ch1--field-mode';property='lsb';value=4},
    @{op='set';id='block-ch1--enum-run';property='name';value='ACTIVE'},
    @{op='set';id='block-ch1--reg-control';property='reset';value='0x10'}
)
$hashBefore = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
$null = Invoke-Cli @('apply',$project,$changes,'--expect',$before,'--dry-run')
Assert ((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -eq $hashBefore) 'Dry run changed the project.'
$saved = Invoke-Cli @('apply',$project,$changes,'--expect',$before,'--session',$session)
Assert ($saved.ok) 'Disk save failed.'
$after = $saved.revision
$pending = Invoke-Cli @('gui',$project,'--session',$session)
Assert ($saved.result.saved -and $saved.result.gui_adopted) 'The native commit did not complete saving and GUI adoption.'
Assert (!$pending.result.pending -and $pending.result.adopted_revision -eq $after) 'Expected the existing window to have adopted the saved revision.'
$null = Invoke-Cli @('apply',$project,$changes,'--expect',$before) 3
$null = Invoke-Cli @('validate',$project)
$adopted = Invoke-Cli @('gui',$project,'--session',$session,'--expect',$after,'--adopt','--select','block-ch1')
Assert ($adopted.result.adopted_revision -eq $after -and $adopted.result.located) 'GUI did not adopt and locate the new Block.'
$located = Invoke-Cli @('gui',$project,'--session',$session,'--expect',$after,'--select','block-ch1--reg-control')
Assert ($located.result.selected_id -eq 'block-ch1--reg-control') 'GUI did not select the copied Register.'
$null = Invoke-Cli @('get-many',$project,'block-ch1--field-mode','block-ch1--enum-run','--expect',$after)
$null = Invoke-Cli @('generate',$project,'--expect',$after)
$null = Invoke-Cli @('status',$project,'--require-current')
[pscustomobject]@{project=$project;session=$session;revision=$after;adopted=$adopted.result.adopted_revision;selected=$located.result.selected_id;trace=$trace} | ConvertTo-Json
if ($CloseWindow) {
    $null = $window.CloseMainWindow()
    if (!$window.WaitForExit(5000)) { throw 'Close the example window manually; it was not forced closed.' }
}
