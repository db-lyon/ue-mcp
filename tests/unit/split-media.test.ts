import { describe, expect, it } from "vitest";
import { splitMedia } from "../../src/dispatch/server-dispatch.js";

describe("splitMedia", () => {
  it("moves image and audio data into content blocks and leaves a pointer in the JSON", () => {
    const { data, blocks } = splitMedia({
      success: true,
      result: { returnValue: "urn:toolresult:X.Y:g:/returnValue" },
      media: [
        { type: "image", mimeType: "image/png", data: "AAAA", uri: "urn:toolresult:X.Y:g:/returnValue", pointer: "/returnValue" },
        { type: "audio", mimeType: "audio/wav", data: "BBBB" },
      ],
    });
    expect(blocks).toEqual([
      { type: "image", mimeType: "image/png", data: "AAAA" },
      { type: "audio", mimeType: "audio/wav", data: "BBBB" },
    ]);
    expect(data).toEqual({
      success: true,
      result: { returnValue: "urn:toolresult:X.Y:g:/returnValue" },
      media: [
        { type: "image", mimeType: "image/png", uri: "urn:toolresult:X.Y:g:/returnValue", pointer: "/returnValue", contentBlock: 1 },
        { type: "audio", mimeType: "audio/wav", contentBlock: 2 },
      ],
    });
    expect(JSON.stringify(data)).not.toContain("AAAA");
  });

  it("leaves a result without media, or with entries it cannot deliver, untouched", () => {
    const plain = { success: true, value: 1 };
    expect(splitMedia(plain)).toEqual({ data: plain, blocks: [] });
    const odd = { media: [{ type: "video", mimeType: "video/mp4", data: "x" }, "text"] };
    expect(splitMedia(odd)).toEqual({ data: odd, blocks: [] });
    expect(splitMedia("text")).toEqual({ data: "text", blocks: [] });
  });
});
