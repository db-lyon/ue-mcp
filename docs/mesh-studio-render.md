# Studio mesh reference images

`editor(render_mesh_studio)` renders static meshes, skeletal meshes in their reference pose, or assemblies of mesh parts as PNG reference images. It supplies its own studio lighting in a disposable preview world. The current level's actors, lights, post processing and viewport camera do not participate.

```json
{
  "action": "render_mesh_studio",
  "args": {
    "items": [
      {
        "id": "cube",
        "meshPath": "/Engine/BasicShapes/Cube.Cube",
        "aspect": { "width": 1, "height": 2 }
      },
      {
        "id": "assembly",
        "parts": [
          { "meshPath": "/Engine/BasicShapes/Cube.Cube" },
          {
            "meshPath": "/Engine/BasicShapes/Cube.Cube",
            "relativeTransform": {
              "location": { "x": 150, "y": 0, "z": 0 },
              "rotation": { "pitch": 0, "yaw": 30, "roll": 0 },
              "scale": { "x": 1, "y": 1, "z": 1 }
            }
          }
        ]
      }
    ],
    "outputDir": "Saved/IconReferences",
    "resolution": 512,
    "viewDirection": { "x": 1, "y": -1.35, "z": 0.9 },
    "lightingPreset": "studio",
    "captureMode": "both",
    "fullyLoadTextures": true
  }
}
```

Each item supplies exactly one of `meshPath` and `parts`. Part transforms use centimetres, a degree rotator and scale relative to a shared assembly origin. Omitted transform fields use zero location/rotation and unit scale. A vector or rotator that is supplied must contain all its axes. Negative scale is supported; zero, nonfinite and excessively large transforms are blocked.

`viewDirection` points from the subject toward the camera. The camera fits every corner of the final union bounds with a margin, a 30-degree horizontal FOV and room for the near plane. The optional `aspect` controls image proportions: the longer edge is `resolution`, and the shorter edge is at least 32 pixels. Without `aspect`, images are square. Resolution must be an integer from 64 to 4096. Aspect width and height are relative numbers from 0.01 to 100.

The studio preset has three broad rectangular lights placed in the camera's frame, with intensity scaled by squared distance, plus a skylight using `/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap`. Set `cubemapPath` to another `TextureCube` asset to use it instead. Exposure is manual, physical-camera exposure is disabled, and bloom, vignette, lens flare, motion blur and temporal antialiasing are disabled. No global rendering settings are written.

| Capture mode | PNG content |
|---|---|
| `beauty` | Lit colour, opaque alpha. |
| `matte` | Coverage in grayscale: white mesh, black background, opaque alpha. |
| `both` (default) | Beauty RGB with coverage alpha reconstructed from a separate inverse-opacity capture. |

The matte uses the renderer's scene-colour inverse-opacity channel. Material translucency and the project's renderer affect its coverage. These are mesh reference captures, with no animation evaluation, ground plane or level background. Conventional streaming texture mips are checked for residency; virtual-texture page convergence is not separately verified.

## Files and results

The call waits while a native job advances on real editor frames. It yields after loading, before skeletal component registration, and waits for shaders and, by default, requested texture mips before capturing. `fullyLoadTextures=false` skips the mip-residency wait. Each item has a 60-second readiness budget and the batch has a 540-second budget. A cold project with substantial shader compilation can report blocked items; retry after compilation settles.

The batch accepts 1-128 items, each with at most 64 parts. An id is a unique filename stem of 1-100 ASCII letters, digits, underscores or hyphens. Case-insensitive duplicates and Windows device names are refused before starting. Outputs are `<outputDir>/<id>.png`; a repeated id in a later batch overwrites that PNG. Relative output directories resolve against the project directory.

The result contains `jobId`, `state`, `outcomes`, `rendered`, `blocked`, `errors`, `manifest`, `cleanupComplete`, `dirtyPackagesBefore`, `dirtyPackagesAfter` and `dirtyPackagesUnchanged`. The job-specific `mesh-studio-<jobId>.json` manifest is written at startup, after each item and after cleanup. Each outcome contains:

- `id` and `status`: `rendered`, `blocked` or `error`, with a reason when unsuccessful.
- For rendered items, `file`, `sizeBytes`, union `bounds` and the actual `camera` location, rotation, direction, FOV, width and height.
- `meshCount`, `textureCount`, `texturesFullyResident` and item duration for captures that reached camera fitting.

Malformed item sources and unloadable/wrong-type assets are blocked independently, so later valid items still render. Invalid batch parameters or unsafe ids refuse the entire call. `success` is true only when every item rendered and the dirty-package verification passed. A completed batch with blocked items still returns its manifest and successful file outcomes.

`editor(get_mesh_studio_render, jobId=...)` reads progress or the final manifest; the last 16 jobs remain queryable until module shutdown. `editor(cancel_mesh_studio_render, jobId=...)` destroys the active preview scene and records unfinished items as blocked. Cancelling an already-finished job returns its existing result.

If the bridge disconnects, the native job continues with its own deadline. A server response with `cleanupPending=true` means the server could not confirm cancellation; query the same job id after reconnecting. Jobs require editor frames to progress.

## Level cleanliness

PIE and simulation are refused, as are concurrent studio jobs. Starting PIE or changing maps during a job cancels it. Preview actors have transient flags, live in a separate transient world, and are explicitly destroyed on completion, error, cancellation and module shutdown. Render targets release their resources between items.

The handler compares the nontransient dirty-package set before and after the job. It never saves packages or clears dirty flags. Existing dirty packages remain dirty. A package-set change during the operation stops the job and reports failure, including when another editor operation caused the change. The handler does not hide an unrelated edit by resetting its dirty flag. `dirtyPackagesUnchanged=true` is the verification to require before accepting a capture batch.

Use `editor(capture_scene_png)` when the desired image is an actor as lit in an existing editor or PIE world. Use `render_mesh_studio` for isolated mesh reference images with their own lighting.
