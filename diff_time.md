# Time Difference
## Request overhead
### Old
- start a new app every request
- uses a lot of memory for multiple requests at the same time
### New
- service -> starts once
- overhead: connection to the LOCAL service (~instant)
- no memory difference
- possible problem: slower response rate for multiple requests at the same time

## /api/files request
### Average: 115.10779000001662 ms over 20000 requests (took ~1:06:40)
### Average: ~85ms over 200 requests (will re-test)
- Added Security Layer (so ~60ms faster AND has an extra layer)