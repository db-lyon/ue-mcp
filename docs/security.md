# Security

ue-mcp and Studio run on your computer, next to the editor. Your project, the editor and the agent's tools stay there. Data leaves the computer for the model provider you choose, for installs and updates, for feedback reports you allow, and, only when you turn on Remote control, through an end-to-end encrypted relay.

## What runs on your computer

- **The bridge plugin** runs inside Unreal Editor and listens on `127.0.0.1` only. It refuses any connection that carries a browser `Origin` header, so no web page can reach it, and it requires a session token kept in a file only your user account can read, which keeps out other local processes and accounts. See [Who may connect](configuration.md#who-may-connect) and [Architecture](architecture.md).
- **The ue-mcp daemon and MCP server** are Node.js processes on your computer. They read your project's files directly and talk to the bridge over that loopback connection. The daemon's HTTP API is also bound to `127.0.0.1` and needs a token.
- **Studio**, its agent and its window run on your computer too. The agent's own tools (`shell`, file reads and edits, search) run there, in your project folder.
- **Credentials** stay on your computer. Studio keeps sign-ins, API keys and custom endpoints in `%USERPROFILE%\.ue-mcp\studio`. It never reads or stores your Claude credentials: it uses the login Claude Code made, through Anthropic's own runtime. The GitHub token for feedback reports is cached at `~/.ue-mcp/auth.json`, readable only by you.

## What leaves your computer

| What | Where it goes | When |
|---|---|---|
| Prompts, and the project context the agent sends with them | The model provider you chose: your MCP client's, or in Studio the sign-in, API key or custom endpoint the model runs on | Every model turn |
| Package and plugin downloads | The npm registry and GitHub releases. Studio checks every download against its SHA-256. | Install and update |
| Feedback reports | A GitHub issue, filed through `feedback.ue-mcp.com`, after your project name, folder paths, username and any credentials are removed on your computer | Only when you approve a report, or set reports to be filed automatically |
| Account sign-in | `accounts.ue-mcp.com` | When you sign in to a ue-mcp account, which Remote control needs |
| Remote control traffic | The relay at `remote.ue-mcp.com`, encrypted end to end | Only while Remote control is on for the project |

With your own MCP client, the conversation runs between that client and its model provider. ue-mcp answers the client's tool calls on your computer.

## Remote control and the relay

[Remote control](remote-control.md) lets a phone or another computer use Studio. The project, the editor and the agent stay on the computer running Studio; the paired device sends requests and shows the answers. Traffic between the two is encrypted end to end and passes through the relay at `remote.ue-mcp.com`, which forwards it but cannot read it.

- Remote control is off until you turn it on, per project. While it is off, paired devices cannot reach the project.
- Pairing takes a one-time code that expires after 10 minutes, and you then allow the device on the computer after checking that both show the same 6-digit code.
- Remote control, pairing and the PIN lock can be changed only on the computer, never from a paired device.
- An optional [PIN lock](remote-control.md#pin-lock), checked by the computer, can be required to open a project or to answer approvals, control the editor, run playtests or keep and revert changes from a paired device.

## When the agent asks for approval

### In Studio

Reads, searches and fetches run without asking. **Settings > Approvals & review** sets whether the agent asks first for each of these, and a new install asks for all of them:

- shell commands;
- file changes;
- restarting or rebuilding the editor;
- ue-mcp actions that delete or overwrite.

A call that asks waits in the chat with the command, the diff or the action until you choose **Allow**, **Allow for this session** or **Deny**. Shell commands and file changes can also be allowed only inside the project folder. Other ue-mcp actions run without asking. See [Approvals](studio.md#approvals).

Changes are still reviewable after they run. With **Review each turn** on, the default, Studio takes a checkpoint around each turn and lists what changed, with **Keep** or **Revert** for each. Editor dialogs that a call opens are shown in the chat, answered by the agent or left in the editor, as you choose under **Editor dialogs**. Playtests start only when you start or assign them, unless you set **Starting playtests** to **Automatic**.

### With your own MCP client

Your client's own permission settings decide which tool calls run. ue-mcp adds its own checks on top:

- An editor dialog a call opens is shown to you as a form in the client when the client supports forms, and is never answered automatically unless you choose that mode. See [Dialog handling mode](configuration.md#dialog-handling-mode).
- A feedback report is shown to you before it is posted, unless you change the [feedback mode](feedback.md#feedback-modes).
- These forms come from the client's interface over the protocol, so the agent cannot answer them for you, and an answer faster than a person could give is not accepted.
- [Guards](guards.md) declared in `ue-mcp.yml` can deny any bridge call before it runs, for example writes outside a folder you choose.

[Checkpoints](checkpoints.md) and the [mutation journal](journal.md) undo changes after the fact on either path.
