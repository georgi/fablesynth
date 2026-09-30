// Run this expression in the DR-1 page on the Vite dev server. Uses an isolated test sequence.
// Assertions cover disclosure, silent selection, fixed-source editing, numeric drafts, and keyboard isolation.
(async () => {
 const {store: useDrumStore, engine: drumEngine} = window.__fableDr;
 const st = useDrumStore.getState;
 const tick = () => new Promise(r => setTimeout(r, 40));
 const checks=[]; window.polyChecks=checks;
 const check=(ok,msg)=>{if(!ok)throw Error(msg);checks.push(msg)};
 const click=async(sel)=>{const e=document.querySelector(sel);if(!e)throw Error(sel);e.click();await tick()};
 useDrumStore.setState({patterns:new Uint8Array(1024),drumRhythm:undefined,chain:[0],powered:true,sel:0,editPattern:2});await tick();
 const cellBefore=document.querySelector('.dr-lanes .step').getBoundingClientRect();
 if(document.querySelector('[aria-controls="dr-poly-panel"]').getAttribute('aria-expanded')==='false') await click('[aria-controls="dr-poly-panel"]');
 check(st().drumRhythm===undefined,'Disclosure does not enable POLY');
 const cellAfter=document.querySelector('.dr-lanes .step').getBoundingClientRect();
 check(cellBefore.x===cellAfter.x&&cellBefore.y===cellAfter.y&&cellBefore.height===cellAfter.height,'Disclosure keeps step geometry');
 check(!document.querySelector('#dr-poly-panel .step'),'Inspector does not duplicate steps');
 let hits=0;const trigger=drumEngine.trigger;drumEngine.trigger=()=>{hits++};
 await click('.dr-lane-name');check(hits===0&&st().sel===15,'Silent lane selection');drumEngine.trigger=trigger;
 await click('[aria-label="Enable lane POLY"]');check(st().drumRhythm.lanes[15].sourceBar===0,'Enable uses bar one');
 check(!document.querySelector('input[name="dr-poly-source"]'),'Source selector is absent');
 await click('input[value="fit"]');check(!!document.querySelector('input[name="dr-poly-cycle"]'),'FIT cycle radios visible');
 st().setEditPattern(0);await tick();
 await click('.dr-lanes .step[data-note="15"][data-abs-step="0"]');
 check(st().patterns[15*16]===1&&st().patterns[(2*16+15)*16]===0,'Source cells edit bar one');
 const input=document.querySelector('input[aria-label="STEPS"]');
 const enter=async(value,key='Enter')=>{
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,value);
  input.dispatchEvent(new Event('input',{bubbles:true}));await tick();
  input.dispatchEvent(new KeyboardEvent('keydown',{key,bubbles:true}));await tick();
 };
 input.focus();await enter('0');check(input.getAttribute('aria-invalid')==='true'&&st().drumRhythm.lanes[15].steps===16,'Invalid draft stays out of engine');
 await click('[aria-label="Enable lane POLY"]');
 await enter('3');check(st().drumRhythm.lanes[15].steps===3,'Direct entry commits');
 check(st().drumRhythm.lanes[15].enabled,'Length edit activates lane');
 const fitted=[...document.querySelectorAll('.dr-lanes .fit-step[data-note="15"]')];
 const fittedRow=fitted[0].parentElement.getBoundingClientRect();
 check(fitted.length===3&&fitted.every((cell,i)=>Math.abs((cell.getBoundingClientRect().x-fittedRow.x)/fittedRow.width-i/3)<.001),'FIT onsets occupy exact cycle thirds');
 await enter('9','Escape');check(input.value==='3'&&st().drumRhythm.lanes[15].steps===3,'Escape restores value');
 const before=st().patterns.slice();
 input.dispatchEvent(new KeyboardEvent('keydown',{key:'Backspace',code:'Backspace',bubbles:true}));await tick();
 check(before.every((v,i)=>st().patterns[i]===v),'Inspector key isolation');
 await click('input[value="grid"]');check(!document.querySelector('input[name="dr-poly-cycle"]'),'GRID hides cycle');
 check(document.querySelectorAll('.dr-lanes .step[data-note="15"].poly-outside').length===13,'GRID retains excluded source notes in place');
 await click('input[value="fit"]');
 document.querySelector('#dr-poly-panel').scrollIntoView({block:'center'});
 return checks;
})()
