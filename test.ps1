$token = (Invoke-WebRequest -Uri "https://best-ranking-search.onrender.com/auth/login?username=admin&password=admin123" -Method POST -UseBasicParsing | ConvertFrom-Json).token

Write-Host "Token: $token"

Invoke-WebRequest -Uri "https://best-ranking-search.onrender.com/index/rebuild" -Method POST -Headers @{"Authorization"="Bearer $token"} -UseBasicParsing