async function runFabLibrary(args) {
  // Use the authenticated Fab frontend session, never the obsolete TEDS route.
  const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
  for (let i = 0; i < 40 && !window.ue?.uemcpfab?.complete; ++i) await pause(50);
  if (!window.ue?.uemcpfab?.complete) return;
  const send = value => window.ue.uemcpfab.complete(args.nonce, JSON.stringify(value));
  const allowed = location.protocol === 'https:' && (!location.port || location.port === '443') &&
    ['fab.com', 'www.fab.com'].includes(location.hostname);
  if (!allowed) return send({success:false,error:'Fab is not on the approved production origin.'});
  const controller = new AbortController();
  window.__ueMcpFabLibraryRequests ??= new Map();
  window.__ueMcpFabLibraryRequests.set(args.nonce, controller);
  const deadline = setTimeout(() => controller.abort(), 280000);
  const clean = () => { clearTimeout(deadline); window.__ueMcpFabLibraryRequests.delete(args.nonce); };
  const read = async url => {
    const response = await fetch(url, {credentials:'include', signal:controller.signal, redirect:'error'});
    if (!response.ok) throw new Error('Fab My Library request failed (HTTP ' + response.status + '). No owned inventory was published.');
    if (!(response.headers.get('content-type') || '').includes('application/json')) throw new Error('Fab returned a sign-in/verification page instead of library JSON.');
    const text = await response.text();
    if (text.length > 16 * 1024 * 1024) throw new Error('Fab library response exceeds the safety limit.');
    return JSON.parse(text);
  };
  const profile = async () => {
    const me = await read('/i/users/me');
    if (typeof me.uid !== 'string' || !me.uid || typeof me.epicId !== 'string' || !me.epicId)
      throw new Error('Fab did not confirm a signed-in account. Sign in inside the editor.');
    if (me.epicId.toLowerCase() !== args.expectedAccount.toLowerCase())
      throw new Error('Fab browser and editor account identities differ. Reauthenticate in Fab before refreshing.');
    return me.uid;
  };
  const strings = value => Array.isArray(value) ? value.filter(v=>typeof v==='string').slice(0,100).map(v=>v.slice(0,256)) : [];
  const normalize = entry => {
    if (!entry || entry.source !== 'acquired' || !entry.listing || typeof entry.listing.uid !== 'string' || typeof entry.listing.title !== 'string')
      throw new Error('Fab returned an unexpected My Library record. No ownership was inferred.');
    const listing = entry.listing;
    return {
      source:entry.source, uid:entry.uid, createdAt:entry.createdAt,
      entitlement:{licenses:(entry.entitlement?.licenses || []).slice(0,20).map(license=>({name:license.name,slug:license.slug}))},
      listing:{uid:listing.uid,title:listing.title.slice(0,1000),listingType:listing.listingType,
        isActiveInLive:listing.isActiveInLive,publisher:{sellerName:listing.publisher?.sellerName},
        assetFormats:(listing.assetFormats || []).slice(0,30).map(format=>({
          assetFormatType:{code:format.assetFormatType?.code},
          technicalSpecs:{
            technicalDetails:typeof format.technicalSpecs?.technicalDetails==='string' ? format.technicalSpecs.technicalDetails.slice(0,8000) : '',
            unrealEngineDistributionMethod:format.technicalSpecs?.unrealEngineDistributionMethod ?? null,
            unrealEngineEngineVersions:strings(format.technicalSpecs?.unrealEngineEngineVersions),
            unrealEngineTargetPlatforms:strings(format.technicalSpecs?.unrealEngineTargetPlatforms)
          }
        }))}
    };
  };
  try {
    const identity = await profile();
    const url = new URL('/i/library/search', location.origin);
    // This is the live UE frontend's My Library / Purchases scope with the
    // "Include 3D compatible formats" option. Never substitute catalog search.
    for (const format of ['converted-files','fbx','glb','gltf','metahuman','unreal-engine']) url.searchParams.append('asset_formats',format);
    url.searchParams.set('channels','unreal-engine');
    url.searchParams.set('source','acquired');
    url.searchParams.set('sort_by','-createdAt');
    const seen = new Set(); let records = 0;
    for (let pageIndex = 1; pageIndex <= 200; ++pageIndex) {
      const body = await read(url);
      if (!Array.isArray(body.results) || !body.cursors || !Object.hasOwn(body.cursors,'next')) throw new Error('Fab returned an invalid library page.');
      const next = body.cursors.next;
      if (next !== null && (typeof next !== 'string' || next.length > 4096)) throw new Error('Fab returned an invalid pagination cursor.');
      if (next && seen.has(next)) throw new Error('Fab repeated a library cursor; partial inventory rejected.');
      const results = body.results.map(normalize); records += results.length;
      if (records > 100000 || (!next && records === 0)) throw new Error('Fab returned an empty or oversized library; no inventory was published.');
      if (!next && await profile() !== identity) throw new Error('Fab account changed during the refresh.');
      if (!next) clean();
      await send({success:true,event:'library_page',pageIndex,endpoint:'/i/library/search',
        page:{results,cursors:{next:next || null}}});
      if (!next) return;
      seen.add(next); url.searchParams.set('cursor',next);
    }
    throw new Error('Fab library exceeded the page limit; partial inventory rejected.');
  } catch (error) {
    clean();
    await send({success:false,error:controller.signal.aborted ? 'Fab library refresh was cancelled or timed out.' :
      (error instanceof SyntaxError ? 'Fab returned malformed JSON.' : String(error.message || 'Fab library refresh failed.').slice(0,500))});
  }
}
