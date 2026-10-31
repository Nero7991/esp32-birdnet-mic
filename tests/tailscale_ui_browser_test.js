// Tailscale card (fork build): hidden without status.tailscale, never pre-fills the key,
// posts the key with the CSRF header, and shows state, address and errors.
const assert=require('node:assert/strict');
const fs=require('node:fs'),zlib=require('node:zlib');
const {chromium}=require('playwright-core');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM||'/snap/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
 for(const packed of [false,true]){
  let source=fs.readFileSync('esp32-birdnet-mic/webui/index.html','utf8');
  if(packed){const h=fs.readFileSync('esp32-birdnet-mic/WebUI_gz.h','utf8');source=zlib.gunzipSync(Buffer.from([...h.matchAll(/0x([0-9a-f]{2})/g)].map(m=>parseInt(m[1],16)))).toString();}
  for(const fork of [false,true]){
   const fixture=JSON.parse(fs.readFileSync('tests/fixtures/ui-state.json','utf8'));
   if(fork)Object.assign(fixture.status,{tailscale:{available:true,has_key:true,state:'connected',ip:'100.76.1.18',hostname:'',peer_path:'direct',last_error:''},stream1_url_tailnet:'rtsp://100.76.1.18:8554/audio1'});
   const writes=[],errors=[];let reply={ok:true,restart:false};
   const page=await browser.newPage({viewport:{width:390,height:844}});
   page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
   await page.route('http://ui.test/**',async route=>{
    const req=route.request(),path=new URL(req.url()).pathname;
    if(path==='/')return route.fulfill({contentType:'text/html',body:source});
    if(req.method()!=='GET'){writes.push({path,values:Object.fromEntries(new URLSearchParams(req.postData())),csrf:req.headers()['x-esp32mic-csrf']});return route.fulfill({json:reply});}
    if(path==='/api/logs')return route.fulfill({body:'Example log'});
    return route.fulfill({json:fixture[path.slice(5)]||{ok:true,available:false,latest:'1.36'}});
   });
   await page.goto('http://ui.test/');await page.waitForFunction(()=>document.querySelector('#in_gain').value!=='');
   const card=page.locator('#tailscalesec');
   if(!fork){assert.equal(await card.isHidden(),true,'card hidden on upstream builds');await page.close();continue;}
   await page.waitForFunction(()=>!document.querySelector('#tailscalesec').hidden);
   await page.evaluate(()=>openSection('tailscalesec'));
   assert.equal(await page.locator('#ts_state').textContent(),'Connected');
   assert.equal(await page.locator('#ts_ip').textContent(),'100.76.1.18');
   assert.equal(await page.locator('#ts_path').textContent(),'Direct');
   assert.match(await page.locator('#ts_url').textContent(),/rtsp:\/\/100\.76\.1\.18:8554\/audio1/);
   const key=page.locator('#in_ts_key');
   assert.equal(await key.getAttribute('type'),'password');
   assert.equal(await key.inputValue(),'');
   // Invalid shape is refused in the browser, nothing is sent.
   await key.fill('not-a-key');await page.locator('#btn_ts_key').click();
   assert.equal(writes.length,0);
   assert.match(await page.locator('#tailscalesec > .feedback').textContent(),/tskey-auth-/);
   // A valid key is posted with the CSRF header; the device restarts.
   reply={ok:true,restart:true};
   await key.fill('tskey-auth-kAbC123CNTRL-0123456789abcdef');await page.locator('#btn_ts_key').click();
   await page.waitForFunction(()=>document.querySelector('#ovr').style.display==='flex');
   assert.deepEqual(writes.at(-1),{path:'/api/tailscale',values:{auth_key:'tskey-auth-kAbC123CNTRL-0123456789abcdef'},csrf:'1'});
   assert.equal(await key.inputValue(),'','key field cleared after sending');
   // Errors from the device are shown, and the saved key is never displayed.
   const html=await page.content();assert(!/tskey-auth-kAbC/.test(html.replace(/value="[^"]*"/g,'')));
   assert.deepEqual(errors,[]);
   await page.close();
  }
  // Error state renders the message.
  const fixture=JSON.parse(fs.readFileSync('tests/fixtures/ui-state.json','utf8'));
  fixture.status.tailscale={available:true,has_key:true,state:'reconnecting',ip:'',hostname:'',peer_path:'',last_error:'Cannot reach Tailscale. Check that this network has internet access.'};
  const page=await browser.newPage({viewport:{width:390,height:844}});
  await page.route('http://ui.test/**',route=>{const path=new URL(route.request().url()).pathname;if(path==='/')return route.fulfill({contentType:'text/html',body:source});if(path==='/api/logs')return route.fulfill({body:''});return route.fulfill({json:fixture[path.slice(5)]||{ok:true}});});
  await page.goto('http://ui.test/');await page.waitForFunction(()=>!document.querySelector('#tailscalesec').hidden);
  assert.equal(await page.locator('#ts_state').textContent(),'Reconnecting');
  assert.match(await page.locator('#ts_err').textContent(),/Cannot reach Tailscale/);
  assert.equal(await page.locator('#ts_ip').textContent(),'Not assigned');
  await page.close();
 }
 }finally{await browser.close();}
 console.log('tailscale ui ok');
})().catch(e=>{console.error(e);process.exit(1);});
