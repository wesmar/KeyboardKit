# ======================================================================
#  Combine source files into a single "code.txt" file
# ======================================================================

param (
    [string]$StartDir = ".",
    [string]$OutputFile = "code.txt"
)

# Determine base directory (for calculating relative paths)
$BaseDir = Resolve-Path $StartDir

# Remove previous output file if it exists
if (Test-Path $OutputFile) {
    Remove-Item $OutputFile
}

# Collect files with specified extensions
$Files = Get-ChildItem -Path $BaseDir -Recurse -Include *.asm, *.cpp, *.h, *.c, *.rc, *.vcxproj, *.filters

foreach ($File in $Files) {
    # Calculate relative path
    $RelativePath = $File.FullName.Substring($BaseDir.Path.Length).TrimStart("\","/")

    # File metadata
    $Item = Get-Item $File.FullName
    $Created   = $Item.CreationTime.ToString("yyyy-MM-dd HH:mm:ss")
    $Modified  = $Item.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
    $SizeKB    = [math]::Round($Item.Length / 1KB, 2)

    # Header
    Add-Content $OutputFile ("================================================================================")
    Add-Content $OutputFile (" FILE: {0}" -f $RelativePath)
    Add-Content $OutputFile (" Creation date : {0}" -f $Created)
    Add-Content $OutputFile (" Modification date: {0}" -f $Modified)
    Add-Content $OutputFile (" File size     : {0} KB" -f $SizeKB)
    Add-Content $OutputFile ("--------------------------------------------------------------------------------")
    Add-Content $OutputFile (" Beginning of file content")
    Add-Content $OutputFile ("--------------------------------------------------------------------------------")
    Add-Content $OutputFile ""

    # Add file content
    Get-Content $File.FullName | Add-Content $OutputFile

    # End of section
    Add-Content $OutputFile "`r`n"
}

Write-Host "Completed. Output file: $OutputFile"