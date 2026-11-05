/**
 * Arguments every editor the test tooling launches carries.
 *
 * The source code accessor is set to one that opens nothing, so no test, no
 * dialog answer and no engine call (AddCodeToProject adds new files to the
 * solution through the accessor) can start an IDE on the developer's desktop.
 * On Windows the null accessor's module does not load, so the editor falls
 * back to its default accessor, which opens nothing either.
 */
export const NO_IDE_ARG = "-ini:EditorSettings:[/Script/SourceCodeAccess.SourceCodeAccessSettings]:PreferredAccessor=NullSourceCodeAccessor";

/**
 * A test editor is never the foreground window, and a throttled one ticks a few
 * times a second, so every call that waits on the game thread waits for a tick.
 */
export const NO_BACKGROUND_THROTTLE_ARG = "-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False";

export const TEST_EDITOR_ARGS = [NO_IDE_ARG, NO_BACKGROUND_THROTTLE_ARG];
