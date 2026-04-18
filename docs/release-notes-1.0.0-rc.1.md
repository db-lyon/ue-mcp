# v1.0.0-rc.1 Release Notes

Relicense to Business Source License 1.1 (BUSL-1.1) with a separate commercial license track for production use outside the Additional Use Grant. This is the first release candidate on the path to v1.0.0. Governance and licensing change only; no functional changes to the server or bridge.

## License change

- **New license:** [Business Source License 1.1](../LICENSE), with Apache 2.0 as the Change License. Each version converts to Apache 2.0 four years after its first publicly available distribution. v1.0.0-rc.1 (April 17, 2026) converts on April 17, 2030.
- **Additional Use Grant** permits free production use by:
  - Individual natural persons using UE-MCP for personal, non-commercial purposes.
  - Enrolled students and accredited educational institutions (bootcamps and non-profit training programs included) using UE-MCP for coursework, academic research, or student projects not incorporated into commercial products.
- **Commercial license required** for all other production use, including game studios, publishers, contract developers, service providers, and commercial entities. See [COMMERCIAL-LICENSE.md](../COMMERCIAL-LICENSE.md) and [ue-mcp.com/pricing](https://ue-mcp.com/pricing).

## Why BUSL

UE-MCP is a dev tool. It runs alongside the Unreal Editor during development and does not ship inside the game binary. Permissive and copyleft licenses trigger on distribution or network service, not on use of a dev tool in a commercial pipeline. BUSL restricts commercial production use of the tool itself, which matches the commercial model directly.

## Prior MIT releases

All releases up to and including **v0.7.19** remain available under the MIT license at the `v0.9-final-mit` tag. Nothing previously released under MIT is retroactively relicensed in place; the MIT grants attached to prior versions are preserved at that tag.

## Contributions

Future contributions are accepted under the Contributor License Agreement in [CLA.md](../CLA.md). The CLA permits UE-MCP's maintainers to distribute contributions under BUSL-1.1, its Change License (Apache 2.0), and one or more commercial licenses.

## No functional changes

This release contains no changes to the MCP server, bridge plugin, tool categories, or actions. Everything that worked in v0.7.19 works the same in v1.0.0-rc.1. The sole change is licensing.
