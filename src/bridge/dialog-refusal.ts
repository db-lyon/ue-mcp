/**
 * True for the refusal the plugin's own gate emits when a modal dialog blocks
 * the editor: any bridge answer carrying `dialogBlocking: true`. Part of the
 * bridge's wire format, so any caller of the bridge can recognise it without
 * depending on the dialog guard that shapes it for the client.
 */
export function isDialogRefusal(v: unknown): boolean {
  return typeof v === "object" && v !== null
    && (v as Record<string, unknown>).dialogBlocking === true;
}
