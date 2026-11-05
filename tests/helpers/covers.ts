/**
 * Claim user-facing surfaces for the integration-coverage inventory
 * (scripts/integration-coverage.ts). Ids come from
 * tests/coverage/integration-surfaces.json. Call it in a test body, or in a
 * describe to claim the surfaces for every test inside it. It does nothing at
 * run time; the inventory reads the source.
 */
export function covers(..._ids: string[]): void {}
