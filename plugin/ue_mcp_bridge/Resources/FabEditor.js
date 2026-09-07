async function runFabEditor(args) {
  // Deliberately no arbitrary script, selector, URL, account, or credential input.
  // This code runs only in an existing, allowlisted Fab editor webview.
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  for (let i = 0; i < 40 && !window.ue?.uemcpfab?.complete; i++) await sleep(50);
  if (!window.ue?.uemcpfab?.complete) return;
  const respond = result => window.ue.uemcpfab.complete(args.nonce, JSON.stringify(result));
  const fail = error => respond({ success: false, error });
  const originOkay = location.protocol === 'https:' && (!location.port || location.port === '443') && ['fab.com', 'www.fab.com'].includes(location.hostname);
  if (!originOkay) return fail('Fab navigated outside the permitted origin. Interaction stopped.');
  const text = node => (node.getAttribute('aria-label') || node.getAttribute('title') || node.innerText || '').trim().replace(/\s+/g, ' ');
  const visible = node => node.isConnected && node.getClientRects().length > 0 && getComputedStyle(node).visibility !== 'hidden';
  const safeHref = node => {
    try {
      const url = new URL(node.getAttribute('href'), location.href);
      return url.protocol === 'https:' && ['fab.com', 'www.fab.com'].includes(url.hostname) ? url.origin + url.pathname : '';
    } catch { return ''; }
  };
  const fingerprint = node => [node.tagName, node.getAttribute('role') || '', text(node), node.getAttribute('href') || ''].join('|');
  const libraryNavigation = node => node.tagName === 'A' && text(node) === 'Purchases' &&
    /\/plugins\/ue5\/library\/?$/.test(safeHref(node));
  const forbidden = /buy|purchase|checkout|cart|wishlist|favorite|favourite|subscribe|follow|place order|payment|install to engine|add to (my )?library|claim|delete|remove|sign out|log out/i;
  const downloadLabel = /^(add to project|download|download now|import|\+)$/i;
  try {
    if (args.operation === 'inspect') {
      const candidates = [...document.querySelectorAll('a[href],button,input,select,[role="button"],[role="tab"],[role="option"],[role="combobox"],[role="radio"]')];
      const nodes = candidates.filter(node => {
        if (!visible(node)) return false;
        if (node.tagName !== 'INPUT') return true;
        return node.type === 'search' || /search/i.test(node.getAttribute('placeholder') || node.getAttribute('aria-label') || '');
      }).slice(0, 300);
      const snapshotId = args.nonce;
      const requests = typeof performance === 'undefined' ? [] : performance.getEntriesByType('resource')
        .filter(entry => ['fetch', 'xmlhttprequest'].includes(entry.initiatorType))
        .map(entry => {
          try {
            const url = new URL(entry.name);
            if (!['fab.com', 'www.fab.com'].includes(url.hostname) || !/^\/(i|e)\//.test(url.pathname)) return null;
            const values = new Set(['is_owned', 'isOwned', 'in', 'view', 'count', 'limit', 'offset', 'sort_by', 'q', 'query', 'asset_formats', 'distribution_method']);
            return { path: url.pathname.replace(/[0-9a-f]{8}-[0-9a-f-]{27,}|[0-9a-f]{32}/gi, '{id}'),
              parameters: Object.fromEntries([...url.searchParams].map(([key,value]) => [key, values.has(key) ? value.slice(0,128) : '[present]'])),
              status: entry.responseStatus || null };
          } catch { return null; }
        }).filter(Boolean).slice(-40);
      window.__ueMcpFabSnapshot = { snapshotId, url: location.href, nodes, fingerprints: nodes.map(fingerprint), at: Date.now() };
      const elements = nodes.map((node, index) => ({
        elementId: String(index), tag: node.tagName.toLowerCase(), role: node.getAttribute('role') || '',
        label: text(node).slice(0, 250), disabled: !!node.disabled || node.getAttribute('aria-disabled') === 'true',
        href: node.tagName === 'A' ? safeHref(node) : undefined,
        options: node.tagName === 'SELECT' ? [...node.options].slice(0, 100).map(option => ({ value: option.value, label: option.label })) : undefined,
        requiresDownloadAuthorization: downloadLabel.test(text(node)), blocked: forbidden.test(text(node)) && !libraryNavigation(node)
      }));
      return respond({ success: true, snapshotId, url: location.origin + location.pathname, elements, requests,
        text: (document.querySelector('main') || document.body).innerText.slice(0, 16000),
        progress: [...document.querySelectorAll('[role="progressbar"],progress')].filter(visible).slice(0, 30).map(node => ({ label: text(node).slice(0, 200), value: node.getAttribute('aria-valuenow') || node.getAttribute('value'), max: node.getAttribute('aria-valuemax') || node.getAttribute('max') })),
        note: 'UI evidence only. Ownership comes exclusively from the native authenticated library result.' });
    }
    const snapshot = window.__ueMcpFabSnapshot;
    if (!snapshot || snapshot.snapshotId !== args.snapshotId || snapshot.url !== location.href || Date.now() - snapshot.at > 120000)
      return fail('Snapshot is stale. Inspect this Fab tab again.');
    if (!/^\d+$/.test(args.elementId)) return fail('Invalid elementId.');
    const index = Number(args.elementId), node = snapshot.nodes[index];
    if (!node || !visible(node) || fingerprint(node) !== snapshot.fingerprints[index]) return fail('The target changed. Inspect again before interacting.');
    if (node.disabled || node.getAttribute('aria-disabled') === 'true') return fail('The selected Fab control is disabled.');
    const label = text(node);
    if (forbidden.test(label) && !libraryNavigation(node)) return fail('Purchases, acquisition, account changes, and engine installation are not supported by this tool.');
    if (args.operation === 'set_search') {
      if (node.tagName !== 'INPUT' || !(node.type === 'search' || /search/i.test(node.getAttribute('placeholder') || node.getAttribute('aria-label') || '')))
        return fail('set_search only accepts a search input returned by inspect.');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(node, args.value);
      node.dispatchEvent(new Event('input', { bubbles: true }));
      node.dispatchEvent(new Event('change', { bubbles: true }));
      node.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', code: 'Enter', bubbles: true }));
    } else if (args.operation === 'select_option') {
      if (node.tagName !== 'SELECT' || ![...node.options].some(option => option.value === args.value && !option.disabled)) return fail('Select an available option value from inspect.');
      Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value').set.call(node, args.value);
      node.dispatchEvent(new Event('input', { bubbles: true }));
      node.dispatchEvent(new Event('change', { bubbles: true }));
    } else if (args.operation === 'download') {
      if (node.tagName !== 'BUTTON' && node.getAttribute('role') !== 'button') return fail('Download target must be a button.');
      if (!downloadLabel.test(label)) return fail('This is not a recognized Fab download/add-to-project control.');
      node.click();
    } else if (args.operation === 'activate') {
      if (downloadLabel.test(label) || /install/i.test(label)) return fail('Use the separately authorized download operation for this control.');
      const role = node.getAttribute('role');
      const navigation = /^(my library|library|next|previous|back|close|cancel|dismiss|show more|load more|filters?|clear filters|all products|unreal engine|format|quality|version|compatibility|include 3d compatible formats)$/i.test(label);
      const dropdown = node.getAttribute('aria-haspopup') === 'listbox' || ['tab', 'option', 'radio', 'combobox'].includes(role);
      const href = node.tagName === 'A' ? safeHref(node) : '';
      const linkAllowed = href && !/\/(cart|checkout|account|settings|wishlist)(\/|$)/i.test(new URL(href).pathname);
      if (!navigation && !dropdown && !linkAllowed) return fail('Unrecognized control. This tool only activates navigation, filters, and format/quality choices.');
      node.click();
    } else return fail('Unsupported interaction.');
    // A successful click is not evidence that a download/import completed.
    delete window.__ueMcpFabSnapshot;
    return respond({ success: true, state: 'submitted', downloadCompleted: false,
      note: 'Inspect again to read the resulting UI. For downloads, verify progress and imported asset readback separately.' });
  } catch {
    return fail('The Fab page changed or rejected the interaction. Inspect again; no automatic retry was performed.');
  }
}
