# ue-mcp v0.5.2 Release Notes

## Architecture Refactor + Linux Support

### DRY Refactor (v0.5.1)

**57 files changed, net -7,787 lines removed** — zero functional changes, all 140 smoke tests passing.

#### C++ HandlerUtils (`HandlerUtils.h`)

New shared utility header eliminates ~7,500 lines of boilerplate across all 21 handler files:

- `MCPError()` / `MCPSuccess()` / `MCPResult()` — one-line response builders replace 3-line error/success patterns
- `RequireString()` / `RequireStringAlt()` — param validation that returns error-or-null, replacing 5-line check-and-return blocks
- `OptionalString()` / `OptionalInt()` / `OptionalBool()` / `OptionalNumber()` — optional param extractors with defaults
- `FindClassByShortName()` — shared utility (was copy-pasted in BlueprintHandlers + LevelHandlers)
- `GetEditorWorld()` / `REQUIRE_EDITOR_WORLD()` — consistent world access (was 3 different patterns)
- `LoadAssetByPath<T>()` / `REQUIRE_ASSET()` — template asset loader with ObjectPath fallback

#### Shared Zod Schemas (`schemas.ts`)

`Vec3`, `Rotator`, `Color`, `Quat` extracted to shared module — 10 tool files updated, ~15 duplicate inline object declarations eliminated.

#### Structured Error Codes (`errors.ts`)

`McpError` class with `ErrorCode` enum (`NOT_CONNECTED`, `BRIDGE_TIMEOUT`, `PROJECT_NOT_LOADED`, etc.) replaces raw `Error` throws. Error responses now include the code: `Error [NOT_CONNECTED]: ...`

#### IBridge Interface

`IBridge` interface in `bridge.ts` decouples tool handlers from the concrete `EditorBridge` class — enables mock-based unit testing.

---

### Linux Platform Support (PR #96)

Thanks to **[@robinduckett](https://github.com/robinduckett)** for the first community contribution!

- Case-sensitive header fix (`PlatformFilemanager.h` → `PlatformFileManager.h`)
- `ILiveCodingModule` guarded behind `PLATFORM_WINDOWS` — HotReload falls back to console command on Linux/Mac
- `LiveCoding` module dependency moved behind `Win64` platform check in `Build.cs`
- Added `<netinet/tcp.h>` for `TCP_NODELAY` on Linux/Mac
- **Fixed broken WebSocket handshake on non-Windows** — was memcpy'ing a 4-byte hash into a 20-byte SHA1 buffer. Replaced with UE's cross-platform `FSHA1`

Tested on UE 5.6 / Ubuntu 25.10 (GCC 15, clang/libc++).
