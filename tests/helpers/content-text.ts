/**
 * The text a tool result's content block carries. A result may hold image and
 * audio blocks beside its text blocks, and those carry no text.
 */
export function blockText(block: object): string | undefined {
  return "text" in block && typeof block.text === "string" ? block.text : undefined;
}

/** Every text block of a result, joined by newlines. */
export function resultText(result: { content: readonly object[] }): string {
  return result.content.map((b) => blockText(b) ?? "").join("\n");
}
