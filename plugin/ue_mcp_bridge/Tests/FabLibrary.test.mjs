import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const script = fs.readFileSync(new URL('../Resources/FabLibrary.js', import.meta.url),'utf8');
const entry = (id, method='asset_pack') => ({source:'acquired',uid:'entry-'+id,
  entitlement:{licenses:[{name:'Personal',slug:'personal'}]},
  listing:{uid:id,title:'Example '+id,listingType:'3d-model',publisher:{sellerName:'Example publisher'},
    assetFormats:[{assetFormatType:{code:'unreal-engine'},technicalSpecs:{
      unrealEngineDistributionMethod:method,unrealEngineEngineVersions:['UE_5.8'],unrealEngineTargetPlatforms:['Windows']}}]}});

async function run(pages, options={}) {
  const events=[], requests=[];
  let page=0, profile=0;
  const fetch = async (input, init) => {
    const url = new URL(input, 'https://www.fab.com');
    requests.push({url,init});
    let body, status=200;
    if (url.pathname === '/i/users/me') {
      profile++; body={uid:options.switchProfile && profile>1 ? 'different-user' : 'profile-1',epicId:options.wrongIdentity ? 'other' : 'account-1'};
    } else {
      assert.equal(url.pathname,'/i/library/search');
      assert.equal(url.searchParams.get('source'),'acquired');
      assert.equal(init.credentials,'include');
      assert.equal(init.redirect,'error');
      body=pages[page++]; status=options.httpStatus || 200;
    }
    return {ok:status===200,status,headers:{get:()=>options.html ? 'text/html':'application/json'},text:async()=>JSON.stringify(body)};
  };
  const window={ue:{uemcpfab:{complete:async(nonce,json)=>events.push(JSON.parse(json))}}};
  const context=vm.createContext({window,location:new URL('https://www.fab.com/plugins/ue5/library'),URL,fetch,AbortController,setTimeout,clearTimeout});
  await vm.runInContext('('+script+')',context)({nonce:'test-operation',expectedAccount:'account-1'});
  return {events,requests,window};
}

test('follows opaque cursors and preserves both delivery contracts', async()=>{
  const r=await run([{results:[entry('one')],cursors:{next:'opaque+/='}},{results:[entry('two','complete_project')],cursors:{next:null}}]);
  assert.equal(r.events.length,2);
  assert.equal(r.events[0].pageIndex,1); assert.equal(r.events[1].pageIndex,2);
  assert.equal(r.events[1].page.results[0].listing.assetFormats[0].technicalSpecs.unrealEngineDistributionMethod,'complete_project');
  assert.equal(r.requests.filter(x=>x.url.pathname==='/i/library/search')[1].url.searchParams.get('cursor'),'opaque+/=');
  assert.equal(r.window.__ueMcpFabLibraryRequests.size,0);
  assert.ok(!JSON.stringify(r.events).includes('account-1'));
});
test('HTTP failures do not fall back to public catalog', async()=>{
  const r=await run([{}],{httpStatus:404});
  assert.equal(r.events.at(-1).success,false); assert.match(r.events.at(-1).error,/HTTP 404/);
  assert.ok(!r.requests.some(x=>x.url.pathname==='/i/listings/search'));
});
test('different native/frontend accounts are rejected before library reads', async()=>{
  const r=await run([],{wrongIdentity:true});
  assert.equal(r.events.at(-1).success,false);
  assert.equal(r.requests.filter(x=>x.url.pathname==='/i/library/search').length,0);
});
test('account change at completion rejects the inventory', async()=>{
  const r=await run([{results:[entry('one')],cursors:{next:null}}],{switchProfile:true});
  assert.equal(r.events.at(-1).success,false); assert.match(r.events.at(-1).error,/account changed/);
});
test('public-shaped records are not relabeled owned', async()=>{
  const item=entry('one'); item.source='catalog';
  const r=await run([{results:[item],cursors:{next:null}}]);
  assert.equal(r.events.at(-1).success,false);
  assert.equal(r.events.filter(x=>x.event==='library_page').length,0);
});
test('repeated cursors cannot publish a completed partial inventory', async()=>{
  const r=await run([{results:[entry('one')],cursors:{next:'repeat'}},{results:[entry('two')],cursors:{next:'repeat'}}]);
  assert.equal(r.events.at(-1).success,false); assert.match(r.events.at(-1).error,/repeated/);
});
test('an empty authenticated response remains explicit, never a fabricated match', async()=>{
  const r=await run([{results:[],cursors:{next:null}}]);
  assert.equal(r.events.at(-1).success,false); assert.match(r.events.at(-1).error,/empty/);
});
test('sign-in HTML is not parsed as owned library data', async()=>{
  const r=await run([],{html:true});
  assert.equal(r.events.at(-1).success,false); assert.match(r.events.at(-1).error,/sign-in/);
});
