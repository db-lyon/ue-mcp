# Remote control

Remote control lets you use [Studio](studio.md) from a phone or another computer. The project, the editor and the agent stay on the computer running Studio; the paired device sends requests to it and shows the answers. Requests and answers between a paired device and the computer are encrypted end to end through the relay at `remote.ue-mcp.com`, so the relay cannot read them.

Remote control needs the Remote plan: $5 a month or $50 a year, before tax. Tax is added at checkout. Studio and ue-mcp themselves are free and open source under the MIT license. See [ue-mcp.com/pricing](https://ue-mcp.com/pricing).

## Requirements

- Studio 0.1.25 or later on the computer running the project. See [Install](studio.md#install).
- A ue-mcp account, which you sign in to with GitHub.
- The Remote plan on that account.
- On the device, a web browser. The Android and iOS apps are a work in progress.

The computer does the work, so it has to be on, with Studio's ue-mcp daemon running and remote control on and connected.

## 1. Sign in

Open **Settings > General**. In the **Account** card, choose **Sign in to your ue-mcp account**. The sign-in is with GitHub: approve it in your browser, or enter the code Studio shows on GitHub. The card then shows **Signed in as @your-login**.

**Settings > Remote control** shows the same sign-in while no account is signed in.

## 2. Upgrade

1. Type `/remote` in a chat, or open **Settings > Remote control**.
2. Pick **Monthly** or **Yearly**, then choose **Upgrade to Remote**.
3. Stripe checkout opens in your browser. Pay there.

Studio picks up the plan a few seconds after the payment goes through, without a restart. If the account already has a subscription, its billing page opens instead of a checkout.

## 3. Turn it on

Remote control is turned on per project.

- The `/remote` card turns it on for this project when it is off, and waits up to 30 seconds for it to connect.
- **Settings > Remote control** has the **Remote control** switch in the **Connection** card. Its state shows below it: **Off**, **Connecting**, **Online** or **Not connected**.

While it is on and connected, devices paired with this computer can reach this project, and the daemon does not stop for being idle. With it off, paired devices cannot reach this project.

## 4. Pair a device

Pairing needs remote control on and **Online**.

1. Get a pairing code. The `/remote` card shows one once it connects. In **Settings > Remote control**, choose **New code** in **Pair a phone or browser**.
2. Scan the QR code with your phone, or open the link on the device.
3. A code works once, for 10 minutes. After that, make a new one.

One pairing covers every project on that computer.

## 5. Allow it on the computer

Studio on the computer then asks whether to allow the device, naming the device and the project, with a code. The device shows a code too.

- Choose **Allow** only if the two codes match. An allowed device can use Studio in every project on this computer.
- Choose **Deny** if they differ.

You have 2 minutes to allow it. After that, make a new code and pair again.

Once allowed, Studio says the device is paired, and it is listed under **Paired devices** in **Settings > Remote control**. **Remove** there unpairs it.

Remote control, pairing and the PIN lock are set up only on the computer. A paired device cannot change them.

## PIN lock

**Settings > Remote control > PIN lock** sets one PIN, 4 to 8 digits, for paired devices, for every project on this computer. The computer checks it, so a device cannot skip it. Studio on the computer itself never asks for it. 10 wrong PINs in a row remove that device; pair it again if that was you.

With a PIN set, two switches choose when a paired device asks for it:

- **Lock Studio on paired devices**: a paired device needs the PIN to use each project, and again after a time without use (**Lock again after**: 5 minutes, 15 minutes or 1 hour), a PIN change or a daemon restart.
- **Ask for sensitive actions**: a paired device needs the PIN for actions such as answering approvals, editor dialogs and forms; starting, stopping, restarting or rebuilding the editor; running playtests or flows; keeping, reverting or undoing changes; deleting a chat; changing settings; and starting a checkout. Settings lists them all. A right PIN allows these on that device, in that project, for 5 minutes.

Removing or changing the PIN, turning a switch off or choosing a longer idle time needs the current PIN. **Forgot PIN** removes the PIN and unpairs every device.

## Notifications

Paired devices can show Studio's notifications. **Settings > Notifications** on each device sets whether that device shows them, what they show, quiet hours, and which projects are muted there.

## Manage or cancel

With the plan active, **Settings > General > Account** has **Manage plan**. It opens the billing portal in your browser, where you can switch between monthly and yearly or cancel. Studio picks up a change within 3 minutes, or as soon as you open Settings.
