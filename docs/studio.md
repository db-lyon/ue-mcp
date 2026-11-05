# UE-MCP Studio

UE-MCP Studio is an agent that works inside Unreal Editor through ue-mcp's actions. You chat with it in a window beside the editor or docked in an editor tab, and it reads and changes your project with the same actions an MCP client gets. Studio is free. Its source is open under the MIT license on GitHub; the copy you get from Fab is licensed under the Fab EULA (Standard License).

Studio comes in three parts:

- **The UE_MCP_Studio editor plugin**, from Fab. It adds the **Studio** toolbar button, **Tools > UE-MCP Studio**, the **UE-MCP Studio** tab with the setup page and a status view, and the **UE-MCP Docs** tab. It starts the project's ue-mcp daemon.
- **ue-mcp and its bridge plugin.** The daemon runs the MCP server for the project, and the bridge plugin connects it to the editor.
- **The Studio extension**: the agent, its UI and `studio-window.exe`, Studio's own window. The ue-mcp daemon loads the extension and serves the UI.

Studio runs in a separate process from the editor, so the chat keeps working while the editor is busy. A long generation or a blocking call shows as "Editor busy" with a timer and a Stop button instead of freezing the chat.

Studio's window has three tabs: **Chat**, **Plugins** and **Settings**. **Feedback** and **Docs** are at the top right.

## Requirements

- Windows 10 or 11.
- Unreal Engine 5.7 or 5.8.
- [Node.js](https://nodejs.org/) 20 or newer. Install the LTS release from the Node.js website with its Windows installer, using the default options. Studio does not install Node.js; when it is missing, the setup page opens the Node.js download page.
- ue-mcp 1.4.11 or later. Studio installs it.
- The Microsoft Edge WebView2 Runtime, which Windows 11 includes. On Windows 10, if it is not already installed, get the Evergreen Runtime from [Microsoft's WebView2 page](https://developer.microsoft.com/en-us/microsoft-edge/webview2/). Without it, Studio opens in your web browser instead of its own window.
- An internet connection, and an account or API key for at least one model provider (see [Models and accounts](#models-and-accounts)).

Studio's own setup does not compile anything: Fab supplies the Studio plugin already built, and Studio installs the bridge as prebuilt binaries for your engine. Visual Studio with the **Desktop development with C++** workload is needed only to build the bridge from source, for example when your project already has a source install of the bridge, which the editor builds when it starts. Get it from the [Visual Studio download page](https://visualstudio.microsoft.com/downloads/) and pick that workload in the Visual Studio Installer.

## Install from Fab

1. Add **UE-MCP Studio** to your library on Fab.
2. In the Epic Games Launcher, install it to your Unreal Engine 5.7 or 5.8 from your Fab Library.
3. Open your project, then enable **UE-MCP Studio** in **Edit > Plugins** and restart the editor when asked.
4. Open Studio with the **Studio** toolbar button or **Tools > UE-MCP Studio**. The console command `Studio.Open` does the same.

## First start

### Consent

The first time you open Studio, the **UE-MCP Studio** tab asks **Allow Studio to install its parts?** Nothing is downloaded or installed until you choose **Allow**. **Not now** closes the tab and records nothing, so Studio asks again the next time you open it.

The prompt lists each part and the folder it goes to:

| Part | Where it goes | Downloaded from |
|---|---|---|
| The UE-MCP bridge plugin | `<YourProject>/Plugins/UE_MCP_Bridge` | GitHub releases of ue-mcp |
| The ue-mcp server, run with `npx` | npm's cache, `%LOCALAPPDATA%\npm-cache\_npx` | registry.npmjs.org |
| The Studio extension | `%USERPROFILE%\.ue-mcp\extensions`, one `studio@<version>` folder per version | GitHub releases of ue-mcp-studio |
| Studio's window app | `%USERPROFILE%\.ue-mcp\studio\window` | Part of the Studio extension |

Your answer is kept per user in `%USERPROFILE%\.ue-mcp\studio\consent.json`, so you are asked once on each machine.

### Setup page

After **Allow**, the tab shows the setup page with three steps.

1. **Dependencies.** Without Node.js, the page offers **Download**, which opens the Node.js download page in your browser, and **Check again** for when the installer has finished.
2. **Bridge.** A page for each group of choices, the same choices `npx ue-mcp init` makes, then **Review and install**. Every choice can be changed later.
    - **Project**: the project, its engine, the bridge port and the installed bridge version. Studio installs prebuilt binaries.
    - **Tools**: the tool categories to turn on. `project`, `editor`, `reflection` and `feedback` are always on.
    - **Native tools**, on Unreal Engine 5.8 only: whether to offer the engine's own MCP tools as actions in the matching categories, and in which categories.
    - **Context**: how much of the tool surface an agent sees at the start of a session: `micro` (recommended), `lean` or `full`.
    - **MCP clients**: other MCP clients found on this machine to connect as well. A client whose config applies to every project is never chosen for you.
    - **Agent**: **Report ue-mcp gaps**, with its [feedback mode](#feedback), and an optional GitHub sign-in so issues are filed as your account. Without the sign-in, issues are filed anonymously on your behalf.
    - **Review**: every choice, each with **Change**.

    **Install** runs `ue-mcp init` with those choices. It installs the bridge plugin, enables the plugins it needs in your `.uproject` (`UE_MCP_Bridge`, `PythonScriptPlugin` and the engine plugins its tool categories use), creates `ue-mcp.yml` in the project folder, and adds `ue-mcp.local.yml` to `.gitignore`. The bridge loads without a restart when every engine plugin it needs is already loaded; otherwise the page asks you to restart the editor once. If the project is already set up, each page starts from its current choices.
3. **MCP server.** The plugin downloads the Studio extension and starts the project's ue-mcp daemon. This can take a few minutes the first time.

Progress is written to `<YourProject>/Saved/Logs/UE_MCP_Studio.log`, and the page shows that path.

Once the daemon is up, the tab shows the status view: the daemon's ue-mcp version and port, whether the editor is connected to Studio, whether the window is open or docked, and whether updates are ready. From then on the plugin starts the daemon with the editor. The daemon keeps running after the editor closes.

## Models and accounts

Pick a model in the chat box, or under **Settings > Agents**. **Settings > Models & accounts** connects the vendors. Each vendor connects through its own sign-in where one is offered, and through your API key where it is not.

| Vendor | Sign-in | API key |
|---|---|---|
| Anthropic | Your Claude Code login | Yes |
| OpenAI | **ChatGPT** | Yes |
| Google | None | Yes |

<img src="images/studio-settings-models.webp" alt="Studio Settings on Models and accounts: the Claude sign-in marked signed in from Claude Code, the ChatGPT sign-in button, the Anthropic, OpenAI and Google API key rows, an empty Custom endpoints card, and the route order with the route each model runs on" style="max-width:100%">

- **Claude.** Studio has no Claude sign-in of its own. Sign in with Claude Code by running `claude /login` in a terminal, and Studio picks up that login on its own. Claude models then run on your Claude plan. Studio reads the login through Anthropic's Claude runtime, which it downloads unmodified from npm the first time it needs it and checks against the registry's hash. Studio never reads or stores your Claude credentials. If Claude Code is not installed, Studio shows a command that runs the same login from its copy of the runtime.
- **ChatGPT.** **Sign in to ChatGPT** opens the sign-in in your browser. OpenAI models then run on your ChatGPT plan.
- **API keys.** **Add key** saves a key for Anthropic, OpenAI or Google. Keys are kept on this machine, in `%USERPROFILE%\.ue-mcp\studio\keys`, and every project uses them.
- **Custom endpoints.** **Add endpoint** adds any OpenAI Chat Completions server, on your machine or hosted: a name, a base URL, an optional key, and the models it lists at `/models`. You can type in models it does not list.
- **Route order.** When a sign-in and a key can both run a model, **Sign-ins first** (the default) or **Keys first** decides which is used, and the other is the fallback. The **Models** table shows each model's routes and the one it runs on.

Sign-ins and keys are kept in `%USERPROFILE%\.ue-mcp\studio` and shared by every project.

## Chat

The agent works in the editor through ue-mcp, and on your machine through Studio's own tools: `shell` (Git Bash when Git is installed, otherwise PowerShell), `read_file`, `write_file`, `edit_file`, `glob`, `grep` and `web_fetch`, plus the model provider's own web search where the route has one. Commands run in the project folder, and relative paths start there.

The chat box has the model and effort pickers, **Checkpoints** and **Attach**. Enter sends, and Shift+Enter starts a new line. While a turn runs, Enter queues the next message and **Stop** ends the turn.

### Mentions

Type `@` and a name to mention an asset, a level actor or a project file. The menu shows each match with its type and path. `@asset:`, `@actor:` and `@file:` narrow it to one kind. A picked match goes into the message with its full path, so the agent can act on it without searching. Actors need the editor running; without it, assets are found from the files on disk.

<img src="images/studio-mention-menu.webp" alt="The mention menu open over the chat box for @Door, listing two C++ files, three assets and a level actor, each with its type and path" style="max-width:100%">

### Commands

Type `/` at the start of a message for the command menu.

- `/action` runs one ue-mcp action directly, named as `category.method`, for example `/action level.get_outliner`. The menu completes the name, and a form asks for its parameters. Your approval settings still apply, so an action that deletes or overwrites can stop at an approval card.
- `/flow` runs one of the project's [flows](flows.md), with a form for its inputs.
- `/playtest` drafts a playtest of what the conversation is about. See [Playtests](#playtests).

### Subagents and the Worker

The agent can hand parts of a task to subagents. Each one shows in the chat with its state, and **Open thread** shows its messages and calls. Under the chat box, the thread list shows the main agent and each subagent with the tokens it has spent this session.

The Worker is Studio's built-in subagent. **Settings > Agents > Worker** sets its model and effort, each **Same as main agent** by default, and when it starts: **When you ask** or **When work splits into independent parts**. The Worker runs on the main agent's sign-in or key.

## Approvals

Reads, searches and fetches run without asking. **Settings > Approvals & review > Ask first** sets, for each kind, whether an agent asks first:

| Kind | Choices |
|---|---|
| Shell commands | **Ask**, **In project** or **Anywhere** |
| File changes | **Ask**, **In project** or **Anywhere** |
| Restart or rebuild the editor | **Ask** or **Allow** |
| ue-mcp actions that delete or overwrite | **Ask** or **Allow** |

**In project** runs shell commands that only name paths inside the project folder, and writes and edits files inside it, without asking; anything else asks. **Anywhere** never asks. A new install asks for all four kinds. The choices are kept per project and apply to every agent. All other ue-mcp actions run without asking.

<img src="images/studio-settings-approvals.webp" alt="Studio Settings on Approvals and review: the Ask first rows for shell commands, file changes, restarting the editor, ue-mcp actions that delete or overwrite, and starting playtests; Feedback reports; Editor dialogs; and the Review each turn switch" style="max-width:100%">

A call that asks waits in the chat, showing the command, the diff or the action, until you choose:

- **Allow** runs this one call.
- **Allow for this session** runs it and stops asking for that kind of call for the rest of the session.
- **Deny** does not run it, and the agent is told you declined.

Stopping the turn while a call waits answers it as stopped, and it does not run.

### Editor dialogs

An editor call waits while a dialog it opened is unanswered. **Settings > Approvals & review > Editor dialogs** sets who answers it:

- **Show in Studio**: a card in the chat with the dialog's buttons. **If unanswered** sets what happens when you do not answer: **Keep waiting**, **Hand to agent** or **Press Cancel or Keep**, after the minutes you set. The window flashes when Studio is not focused.
- **Agent answers**: the agent picks the answer. An answer that discards work waits 10 seconds for you to override it.
- **Leave in editor**: the turn waits until you answer in the editor.

## Turn review and checkpoints

With **Settings > Approvals & review > Review each turn** on, which is the default, Studio takes a checkpoint before and after each turn. Edits apply to the project as the agent makes them. After a turn that changed files, a card in the chat lists each change by type, with **Keep** or **Revert** for each and **Revert all**. **Undo revert** puts a reverted change back. A file you edited after the agent shows as a conflict, and reverting it asks you first.

- **Assets** show Unreal's own diff of the two versions: what was added, removed or changed, with the values before and after, and a picture of each version where the editor can render one.
- **Blueprints** also show a Blueprint diff of their graphs, components, variables and defaults.
- **Open in Unreal diff** opens the engine's own diff of the asset in the editor.
- **Text files** show a line diff.

<img src="images/studio-review-blueprint.webp" alt="A turn review card with a Blueprint opened: pictures before and after, then the Blueprint diff listing an added Branch node, an added Print String node with its values, a changed component property, an added variable and changed defaults, with Open in Unreal diff, Keep and Revert" style="max-width:100%">

**Checkpoints**, in the chat box, opens the session's checkpoint timeline. **Restore to here** puts the project's changed files back as they were at that checkpoint, and **Undo** reverses the restore. **Review this session's turns** there turns review on or off for one session, overriding the setting.

The checkpoints are ue-mcp's. See [Checkpoints](checkpoints.md) for where they are kept.

## Playtests

A playtest is a Play In Editor test script of steps, each checked automatically by an observer or judged by you. Playtests are ue-mcp [test scripts](pie-scripts.md).

- `/playtest`, with what to test or nothing, drafts a playtest of what the conversation is about. After a turn that changed something, **Make playtest** drafts one of what that turn changed.
- The draft appears as a card in the chat. **Edit** changes its steps, and **Start playtest** runs it. A step you judge waits on the card with **Pass**, **Fail** and **Skip**.
- **Assign to subagent** hands the playtest to a subagent. It runs the playtest, changes the project to fix what fails, and runs it again until every step passes or it needs you. Each attempt shows what it changed, with **Undo its changes**, **See what it did** and **Revert every attempt**. When it needs you, the card says why, with **Assign again** and **End loop**. Assigning needs turn review on.

<img src="images/studio-playtest-loop.webp" alt="A playtest assigned to a subagent that needs you: the reason, two attempts with Undo its changes and See what it did, Revert every attempt, and the five steps with their observer or person checks and results" style="max-width:100%">

**Settings > Approvals & review > Starting playtests** sets whether playtests run only when you start or assign them (**Manual**, the default) or agents start them when needed (**Automatic**).

## Docked or window

Studio opens in its own window. The window stays above the editor while either is in front, minimizes and restores with it, and remembers its place for each project.

**Dock in editor**, in Studio's header or in the **UE-MCP Studio** tab, shows Studio in that tab instead, beside Details by default. Docked, it is still its own process, held over the tab: it moves, resizes, floats and hides with the tab like any other panel. **Open as window** brings it back out. The choice is kept per project. When the tab is part of your saved editor layout, Studio appears in it on editor start.

### Ask Studio from the editor

- **Ctrl+Alt+Space**, or **Ctrl+Alt+right-click** over the level viewport or a graph, opens a one-line prompt at the cursor that starts a new Studio session. It carries what is under the cursor as context. The shortcut can be changed in **Editor Preferences > Keyboard Shortcuts** under **Ask Studio**.
- **Chat about this**, in the context menu of selected actors or assets, opens the same prompt with them as context.

<img src="images/studio-ask-prompt.webp" alt="The Ask Studio prompt over a selected actor in the level viewport, with a reply explaining why its material looks washed out, and the Continue this thread and Close buttons" style="max-width:100%">

<img src="images/studio-ask-graph.webp" alt="The Ask Studio prompt over a Transform Points node in a PCG graph, with a reply about the rotation range of the points it outputs, and the Continue this thread and Close buttons" style="max-width:100%">

The prompt shows the reply, with **Continue this thread**, which opens the session in Studio.

## Settings

| Section | What it holds |
|---|---|
| **General** | The theme (**ue-mcp**, **Editor orange** or **Editor**) and the **Updates** card. |
| **Agents** | The main agent's model and effort, the screenshot size shown to agents, the **Worker**, and **Project agent files**: whether agents read the instructions, subagents and skills in the project and your home folder, with a read-only list of them. |
| **Models & accounts** | Sign-ins, API keys, custom endpoints and the route order. |
| **Approvals & review** | **Ask first**, **Starting playtests**, **Feedback reports**, **Editor dialogs** and **Review each turn**. |
| **Project** | Whether the project's ue-mcp setup is ready, with what needs attention; the context strategy other MCP clients load; the MCP clients configured; the setup pages; the bridge plugin's version and install; and `ue-mcp.yml` under **Advanced**. |
| **Diagnostics** | The editor and daemon status, and recent daemon events. |

The **Plugins** tab lists the project's ue-mcp [plugins](plugins.md), with update and remove, and the registry's catalog to install from. Changes load when the daemon restarts.

## Updates

- **The editor plugin** installed from Fab updates through your Fab Library in the Epic Games Launcher. The plugin records at startup that it is the Fab build, in `<YourProject>/Saved/UE_MCP_Studio/plugin.json`, and Studio never replaces a plugin that reports itself as the Fab build. When a Studio release needs a newer plugin, Studio says so and offers **Open Fab Library**.
- **Studio, the bridge plugin and ue-mcp** update in the app. The **Updates** card in **Settings > General** says whether an update is available, and **Update** installs everything that is ready. **Details** lists Studio, the editor plugin, the bridge plugin and ue-mcp with the installed and latest versions. The editor plugin also checks for a newer Studio each time the editor starts and hands it to the same updater.
- An update that restarts ue-mcp or the editor waits until no turn is running. **Restart now** installs it at once and stops the turn.
- **Channel** picks **Stable** or **Beta**. Beta also takes prereleases, for testing. The channel is kept per user.

### Studio versions per project

Each project keeps its own Studio version, so updating Studio in one project does not change another. When another project has installed a newer Studio, a notice says so, with **Update this project**, which moves this project to that version and restarts its daemon, and **Keep**, which keeps the current version and hides the notice until a newer version arrives.

<img src="images/studio-version-notice.webp" alt="The notice above the chat: Studio 0.1.25 was installed by another project, this project uses 0.1.23, with Update this project and Keep 0.1.23" style="max-width:100%">

## Feedback

**Feedback**, at the top right of Studio, files **Report a problem** or **Suggest an idea** as a GitHub issue on Studio's tracker. You need no GitHub account. The report can include the installed versions, engine, OS and model. If the feedback service cannot take it, the report opens on GitHub filled in, to send from there.

Agents can also report gaps they find in ue-mcp as GitHub issues on ue-mcp. Your project name, its folder paths and your username are removed from every issue first. **Settings > Approvals & review > Feedback reports** turns this on or off, and **Filing** sets how reports are filed:

- **Ask**: edit, approve or discard each report in the chat.
- **File automatically**: reports go straight to GitHub. The setup page recommends this mode.
- **Hold**: reports wait for you to review them with `npx ue-mcp feedback review`.

Turning reports on or off restarts ue-mcp. See [Feedback](feedback.md).

## Uninstall

1. Close Studio, then stop the project's daemon, which keeps running after the editor closes. From the project folder:

    ```bash
    npx ue-mcp daemon stop
    ```

2. Disable **UE-MCP Studio** in **Edit > Plugins** for each project that uses it, then close the editor. To remove the plugin from your engine, remove it through your Fab Library in the Epic Games Launcher.
3. Delete `<YourProject>/Plugins/UE_MCP_Bridge` and `<YourProject>/Saved/UE_MCP_Studio`, and remove the `UE_MCP_Bridge` and `UE_MCP_Studio` entries from the `Plugins` list in your `.uproject`. Delete `ue-mcp.yml` from the project folder if you do not use ue-mcp otherwise.
4. Delete Studio's per-user folders:
    - The `studio` and `studio@<version>` folders in `%USERPROFILE%\.ue-mcp\extensions`: the Studio extension.
    - `%USERPROFILE%\.ue-mcp\studio`: the window app, the Claude runtime, the ChatGPT sign-in, API keys, custom endpoints, the update channel and the consent record.
    - `%USERPROFILE%\.ue-mcp\data`: per-project data, including Studio's settings and sessions.
    - `%USERPROFILE%\.ue-mcp\daemons`: the daemons' discovery files.

    If you do not use ue-mcp for anything else, delete all of `%USERPROFILE%\.ue-mcp`.
5. Optionally, delete npm's `npx` cache at `%LOCALAPPDATA%\npm-cache\_npx`. It holds the ue-mcp package and anything else you ran with `npx`.

## Source

- Editor plugin: [db-lyon/ue-mcp-studio-plugin](https://github.com/db-lyon/ue-mcp-studio-plugin)
- Studio extension: [db-lyon/ue-mcp-studio](https://github.com/db-lyon/ue-mcp-studio)
- ue-mcp and the bridge plugin: [db-lyon/ue-mcp](https://github.com/db-lyon/ue-mcp)
