import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const script = fs.readFileSync(new URL('../Resources/FabEditor.js', import.meta.url), 'utf8');

function fixture(label = 'My Library', tag = 'BUTTON', extras = {}) {
  const attributes = { ...extras };
  const node = {
    tagName: tag, innerText: label, isConnected: true, disabled: false, clicked: 0,
    getAttribute: key => attributes[key] ?? null,
    getClientRects: () => [1], click() { this.clicked++; },
    dispatchEvent() {}, attributes
  };
  const replies = [];
  const location = new URL('https://www.fab.com/plugins/ue5/listings/owned-item');
  const window = { ue: { uemcpfab: { complete(nonce, json) { replies.push({ nonce, ...JSON.parse(json) }); } } } };
  const document = {
    querySelectorAll: selector => selector.startsWith('[role="progressbar"') ? [] : [node],
    querySelector: () => ({ innerText: 'Fab product details' }), body: { innerText: '' }
  };
  const context = vm.createContext({ window, document, location, URL, setTimeout, Date,
    getComputedStyle: () => ({ visibility: 'visible' }), Event: class {}, KeyboardEvent: class {} });
  const run = vm.runInContext(`(${script})`, context);
  return { node, replies, window, location, async inspect() {
    await run({ operation: 'inspect', nonce: 'snapshot-1' }); return replies.at(-1);
  }, async act(operation = 'activate') {
    await run({ operation, nonce: 'operation-2', snapshotId: 'snapshot-1', elementId: '0', value: '' }); return replies.at(-1);
  }, run };
}

test('inspect produces handles without clicking', async () => {
  const f = fixture(); const r = await f.inspect();
  assert.equal(r.success, true); assert.equal(r.elements[0].elementId, '0'); assert.equal(f.node.clicked, 0);
});
test('navigation uses a fresh snapshot', async () => {
  const f = fixture(); await f.inspect(); const r = await f.act();
  assert.equal(r.success, true); assert.equal(f.node.clicked, 1); assert.equal(r.state, 'submitted');
});
test('purchases are blocked', async () => {
  const f = fixture('Buy now'); await f.inspect(); const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
test('engine installation is blocked', async () => {
  const f = fixture('Install to engine'); await f.inspect(); const r = await f.act('download');
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
test('normal activation cannot submit downloads', async () => {
  const f = fixture('Add to project'); await f.inspect(); const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
test('download reports submission, never completion', async () => {
  const f = fixture('Add to project'); await f.inspect(); const r = await f.act('download');
  assert.equal(r.success, true); assert.equal(r.downloadCompleted, false); assert.equal(f.node.clicked, 1);
});
test('changed control fails closed', async () => {
  const f = fixture(); await f.inspect(); f.node.innerText = 'Next'; const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
test('disabled controls are not clicked', async () => {
  const f = fixture(); await f.inspect(); f.node.disabled = true; const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
test('navigation invalidates the snapshot', async () => {
  const f = fixture(); await f.inspect(); f.location.pathname = '/plugins/ue5/library'; const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
test('an unknown button is not automatically activated', async () => {
  const f = fixture('Do something'); await f.inspect(); const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
test('a second click needs a new snapshot', async () => {
  const f = fixture(); await f.inspect(); await f.act(); const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 1);
});
test('off-origin navigation is rejected', async () => {
  const f = fixture(); await f.inspect(); f.location.hostname = 'example.com'; const r = await f.act();
  assert.equal(r.success, false); assert.equal(f.node.clicked, 0);
});
