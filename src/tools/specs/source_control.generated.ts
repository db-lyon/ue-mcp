// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd source_control handler. */
export const handlerSpecs: HandlerSpecs = {
  "source_control_add": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)",
        "items": "string"
      }
    ],
    "contractExempt": "Marks files for add in the configured provider when called"
  },
  "source_control_checkout": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)",
        "items": "string"
      }
    ],
    "contractExempt": "Checks files out of the configured provider when called"
  },
  "source_control_connect": {
    "category": "source_control",
    "params": [
      {
        "name": "password",
        "type": "string",
        "required": false,
        "description": "Password or ticket for providers that log in (Perforce); sent to the provider only, never echoed"
      }
    ],
    "contractExempt": "Connects the editor's configured provider when called"
  },
  "source_control_diff": {
    "category": "source_control",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "One asset path or file to diff against its latest repository revision"
      },
      {
        "name": "openEditor",
        "type": "boolean",
        "required": false,
        "description": "For a package, open the editor's asset diff window (default false)"
      },
      {
        "name": "contextLines",
        "type": "integer",
        "required": false,
        "description": "Unchanged lines around each text change (default 3)"
      },
      {
        "name": "maxLines",
        "type": "integer",
        "required": false,
        "description": "Most diff lines to return (default 400); truncated says when cut"
      }
    ]
  },
  "source_control_get_provider": {
    "category": "source_control",
    "params": []
  },
  "source_control_get_status": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)",
        "items": "string"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most files to report (default 500); truncated says when more matched"
      }
    ]
  },
  "source_control_history": {
    "category": "source_control",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": true,
        "description": "One asset path or file whose revision history to read"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most revisions to return, newest first (default 20); truncated says when more exist"
      }
    ]
  },
  "source_control_list_changes": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": false,
        "description": "Folders or files to look under (default the project's Content, Config, Source and Plugins folders and its .uproject)",
        "items": "string"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Most files to report (default 500); truncated says when more matched"
      },
      {
        "name": "includeUntracked",
        "type": "boolean",
        "required": false,
        "description": "Also list files on disk the provider does not track (default true)"
      }
    ]
  },
  "source_control_mark_for_delete": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)",
        "items": "string"
      }
    ],
    "contractExempt": "Marks files for delete in the configured provider when called"
  },
  "source_control_revert": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)",
        "items": "string"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report what would be reverted and lost, and change nothing"
      },
      {
        "name": "discardUnsaved",
        "type": "boolean",
        "required": false,
        "description": "Drop unsaved editor edits to the named packages instead of refusing (default false)"
      },
      {
        "name": "restoreFrom",
        "type": "object",
        "required": false,
        "description": "File to saved copy, as mark_for_delete's rollback carries it: a deleted file the provider cannot revert is put back from its copy and marked for add"
      }
    ],
    "contractExempt": "Reverts files in the configured provider when called"
  },
  "source_control_set_checkout_mode": {
    "category": "source_control",
    "params": [
      {
        "name": "mode",
        "type": "string",
        "required": true,
        "description": "manual refuses a write to a read-only file; auto checks it out first",
        "enum": [
          "manual",
          "auto"
        ]
      }
    ],
    "contractExempt": "Changes the editor session's checkout mode when called"
  },
  "source_control_submit": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": true,
        "description": "Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project)",
        "items": "string"
      },
      {
        "name": "description",
        "type": "string",
        "required": true,
        "description": "The change description or commit message"
      }
    ],
    "contractExempt": "Submits files to the repository when called"
  },
  "source_control_sync": {
    "category": "source_control",
    "params": [
      {
        "name": "paths",
        "type": "array",
        "required": false,
        "description": "Folders or files to sync to the latest revision (default the project's Content, Config, Source and Plugins folders and its .uproject)",
        "items": "string"
      }
    ],
    "contractExempt": "Syncs the workspace from the repository when called"
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  source_control_add: "Params: paths",
  source_control_checkout: "Params: paths",
  source_control_connect: "Params: password?",
  source_control_diff: "Params: path, openEditor?, contextLines?, maxLines?",
  source_control_get_provider: "Params: none",
  source_control_get_status: "Params: paths, limit?",
  source_control_history: "Params: path, limit?",
  source_control_list_changes: "Params: paths?, limit?, includeUntracked?",
  source_control_mark_for_delete: "Params: paths",
  source_control_revert: "Params: paths, dryRun?, discardUnsaved?, restoreFrom?",
  source_control_set_checkout_mode: "Params: mode",
  source_control_submit: "Params: paths, description",
  source_control_sync: "Params: paths?",
};

/** Every key the spec'd source_control handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  contextLines: z.number().int().optional().describe("Unchanged lines around each text change (default 3)"),
  description: z.string().optional().describe("The change description or commit message"),
  discardUnsaved: z.boolean().optional().describe("Drop unsaved editor edits to the named packages instead of refusing (default false)"),
  dryRun: z.boolean().optional().describe("Report what would be reverted and lost, and change nothing"),
  includeUntracked: z.boolean().optional().describe("Also list files on disk the provider does not track (default true)"),
  limit: z.number().int().optional().describe("Most files to report (default 500); truncated says when more matched (source_control_get_status, source_control_list_changes). Most revisions to return, newest first (default 20); truncated says when more exist (source_control_history)"),
  maxLines: z.number().int().optional().describe("Most diff lines to return (default 400); truncated says when cut"),
  mode: z.enum(["manual","auto"]).optional().describe("manual refuses a write to a read-only file; auto checks it out first"),
  openEditor: z.boolean().optional().describe("For a package, open the editor's asset diff window (default false)"),
  password: z.string().optional().describe("Password or ticket for providers that log in (Perforce); sent to the provider only, never echoed"),
  path: z.string().optional().describe("One asset path or file to diff against its latest repository revision (source_control_diff). One asset path or file whose revision history to read (source_control_history)"),
  paths: z.array(z.string()).optional().describe("Asset or package paths (/Game/X), content folders, or files on disk (absolute or relative to the project) (source_control_add, source_control_checkout, source_control_get_status, source_control_mark_for_delete, source_control_revert, source_control_submit). Folders or files to look under (default the project's Content, Config, Source and Plugins folders and its .uproject) (source_control_list_changes). Folders or files to sync to the latest revision (default the project's Content, Config, Source and Plugins folders and its .uproject) (source_control_sync)"),
  restoreFrom: z.record(z.string(), z.unknown()).optional().describe("File to saved copy, as mark_for_delete's rollback carries it: a deleted file the provider cannot revert is put back from its copy and marked for add"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
