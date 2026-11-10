/**
 * The Unreal Canvas Bridge answers through Epic's MCP tool result shape: a
 * `content` array of text blocks. Its render tools put a file or directory
 * path in each block's text and describe it in the block's `annotations`; the
 * rest put a JSON document in the first block. These turn that into fields an
 * agent can read without parsing text: image and frame-directory paths with
 * their pass names, or the tool's JSON as top-level fields.
 */

/** A file inside a directory the editor named, in that path's own separator style. */
function inDir(dir: string, file: string): string {
  const sep = dir.includes("\\") && !dir.includes("/") ? "\\" : "/";
  return dir.replace(/[\\/]+$/, "") + sep + file;
}

type Json = Record<string, unknown>;

const isObject = (v: unknown): v is Json => v !== null && typeof v === "object" && !Array.isArray(v);
const str = (v: unknown): string | undefined => (typeof v === "string" && v !== "" ? v : undefined);
const num = (v: unknown): number | undefined => (typeof v === "number" && Number.isFinite(v) ? v : undefined);

/** A JSON value from a string the Bridge serialized, or the string itself when it does not parse. */
function parsed(v: unknown): unknown {
  if (typeof v !== "string") return v;
  const t = v.trim();
  if (!t.startsWith("{") && !t.startsWith("[")) return v;
  try {
    return JSON.parse(t);
  } catch {
    return v;
  }
}

function textBlocks(answer: Json): Array<{ text: string; annotations: Json }> {
  const content = Array.isArray(answer.content) ? answer.content : [];
  return content
    .filter((b): b is Json => isObject(b) && b.type === "text" && typeof b.text === "string")
    .map((b) => ({ text: b.text as string, annotations: isObject(b.annotations) ? b.annotations : {} }));
}

/** The answer without the raw MCP blocks, which the shaped fields replace. */
function rest(answer: Json): Json {
  const { content: _content, structuredContent: _structured, ...others } = answer;
  return others;
}

/** Strip undefined so a shaped record carries only what the Bridge said. */
function compact<T extends Json>(o: T): T {
  for (const k of Object.keys(o)) if (o[k] === undefined) delete o[k];
  return o;
}

/**
 * TriggerMRGRender: one PNG per capture, plus RawDepth.npy for a depth
 * capture. Returns `images` (path, passName, branchName, subResourceName,
 * camera, spatialAnnotations), `rawDepth` and the first non-empty
 * `sceneStateBlobPath`, which render_reposition and save_scene_snapshot take.
 */
export function shapeStillRender(answer: Json): Json {
  const images: Json[] = [];
  const rawDepth: Json[] = [];
  let sceneStateBlobPath: string | undefined;
  for (const { text, annotations: a } of textBlocks(answer)) {
    const entry = compact({
      path: text,
      passName: str(a.passName),
      branchName: str(a.branchName),
      subResourceName: str(a.subResourceName),
      camera: a.Camera === undefined ? undefined : parsed(a.Camera),
      spatialAnnotations: a.spatial_annotations === undefined ? undefined : parsed(a.spatial_annotations),
    });
    sceneStateBlobPath ??= str(a.sceneStateBlobPath);
    if (/\.npy$/i.test(text) || a.passName === "RawDepth") rawDepth.push(entry);
    else images.push(entry);
  }
  return compact({ ...rest(answer), images, rawDepth: rawDepth.length > 0 ? rawDepth : undefined, sceneStateBlobPath });
}

/** A printf-style frame pattern ("frame.%04d.png") filled with one frame number. */
export function frameFile(pattern: string, frame: number): string {
  return pattern.replace(/%(0?)(\d*)d/, (_m, zero: string, width: string) => {
    const digits = String(Math.trunc(frame));
    const w = Number(width) || 0;
    return digits.length >= w ? digits : (zero ? "0" : " ").repeat(w - digits.length) + digits;
  });
}

/**
 * TriggerSequenceRender and BuildAndRenderRepositionSequence: one directory
 * of PNG frames per capture. Returns `passes` with the directory, its frame
 * pattern and count, and the first and last frame's full path.
 */
export function shapeSequenceRender(answer: Json): Json {
  const passes = textBlocks(answer).map(({ text, annotations: a }) => {
    const pattern = str(a.filePattern);
    const start = num(a.startFrame);
    const count = num(a.frameCount);
    return compact({
      directory: text,
      passName: str(a.passName),
      branchName: str(a.branchName),
      subResourceName: str(a.subResourceName),
      frameCount: count,
      fps: num(a.fps),
      filePattern: pattern,
      startFrame: start,
      sequenceName: str(a.sequenceName),
      firstFrame: pattern && start !== undefined ? inDir(text, frameFile(pattern, start)) : undefined,
      lastFrame: pattern && start !== undefined && count ? inDir(text, frameFile(pattern, start + count - 1)) : undefined,
    });
  });
  return { ...rest(answer), passes };
}

/** SaveSceneSnapshot: the text is the saved Level Snapshot's object path. */
export function shapeAssetPath(answer: Json): Json {
  const first = textBlocks(answer)[0]?.text;
  return compact({ ...rest(answer), assetPath: first });
}

/** Every other tool: the first text block is a JSON document, merged in as fields. */
export function shapeJson(answer: Json): Json {
  const blocks = textBlocks(answer);
  const doc = blocks.length > 0 ? parsed(blocks[0].text) : undefined;
  if (isObject(doc)) return { ...rest(answer), ...doc, tool: answer.tool };
  if (doc === undefined) return rest(answer);
  return { ...rest(answer), result: doc };
}

export type CanvasResultShape = "still" | "sequence" | "assetPath" | "json";

const SHAPERS: Record<CanvasResultShape, (a: Json) => Json> = {
  still: shapeStillRender,
  sequence: shapeSequenceRender,
  assetPath: shapeAssetPath,
  json: shapeJson,
};

/** The Bridge answer in `shape`; a failed or unexpected answer passes through untouched. */
export function shapeCanvasAnswer(shape: CanvasResultShape, answered: unknown): unknown {
  if (!isObject(answered) || answered.success === false || !Array.isArray(answered.content)) return answered;
  return SHAPERS[shape](answered);
}
