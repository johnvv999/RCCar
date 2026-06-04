git add -A
git commit -m "Auto-sync $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" 
git push

Write-Host "`n--- Verification ---`n"
git status
git log -1
Pause
