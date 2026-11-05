/**
 * Services extensions provide to each other in process (feature
 * "extension.services"). One provider holds an id at a time. Extensions load
 * in directory order, so a consumer may activate before its provider and
 * learns of it through `watch`. A provider's services end when it is disposed.
 */
import { warn } from "../core/log.js";
import type { ExtensionServicesApi } from "./extension-api.js";

interface Watcher {
  owner: string;
  cb: (impl: unknown) => void;
}

export class ExtensionServices {
  private readonly provided = new Map<string, { owner: string; impl: unknown }>();
  private readonly watchers = new Map<string, Set<Watcher>>();

  /** Provide `impl` under `id`. Throws while another provider holds the id. Returns the revoke. */
  provide(owner: string, id: string, impl: unknown): () => void {
    const held = this.provided.get(id);
    if (held) throw new Error(`service ${id} is already provided by ${held.owner}`);
    const entry = { owner, impl };
    this.provided.set(id, entry);
    this.notify(id, impl);
    return () => {
      if (this.provided.get(id) !== entry) return;
      this.provided.delete(id);
      this.notify(id, undefined);
    };
  }

  get(id: string): unknown {
    return this.provided.get(id)?.impl;
  }

  /** Call `cb` now if `id` is provided, on each provide, and with undefined on each revoke. */
  watch(owner: string, id: string, cb: (impl: unknown) => void): () => void {
    const w: Watcher = { owner, cb };
    let set = this.watchers.get(id);
    if (!set) this.watchers.set(id, (set = new Set()));
    set.add(w);
    const held = this.provided.get(id);
    if (held) this.call(id, w, held.impl);
    return () => {
      set.delete(w);
    };
  }

  /** A disposed extension: its watches stop first, then its services are revoked and their watchers told. */
  revokeOwner(owner: string): void {
    for (const set of this.watchers.values()) for (const w of set) if (w.owner === owner) set.delete(w);
    for (const [id, e] of [...this.provided]) {
      if (e.owner !== owner) continue;
      this.provided.delete(id);
      this.notify(id, undefined);
    }
  }

  /** The `api.services` an extension is handed. */
  forOwner(owner: string): ExtensionServicesApi {
    return {
      provide: (id, impl) => this.provide(owner, id, impl),
      get: <T>(id: string) => this.get(id) as T | undefined,
      watch: <T>(id: string, cb: (impl: T | undefined) => void) => this.watch(owner, id, cb as (impl: unknown) => void),
    };
  }

  private notify(id: string, impl: unknown): void {
    for (const w of [...(this.watchers.get(id) ?? [])]) this.call(id, w, impl);
  }

  private call(id: string, w: Watcher, impl: unknown): void {
    try {
      w.cb(impl);
    } catch (e) {
      warn("extension", `${w.owner}'s watcher for service ${id} threw`, e);
    }
  }
}
