$r = Invoke-WebRequest -Uri "https://best-ranking-search.onrender.com/auth/login?username=admin&password=admin123" -Method POST -UseBasicParsing
$token = ($r.Content | ConvertFrom-Json).token
Invoke-WebRequest -Uri "https://best-ranking-search.onrender.com/crawl?url=http://example.com&depth=1&pages=3" -Method POST -Headers @{"Authorization"="Bearer $token"} -UseBasicParsing