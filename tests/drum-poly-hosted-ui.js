// Run on the powered SQ-4 page of the Vite dev server with an isolated test session.
(async () => {
 const seq=window.__fableSq.store; const code=await fetch('/src/seq/components/DeviceView.tsx').then(r=>r.text()); const url=code.match(/from "([^"]*\/drum\/store\.ts[^"]*)"/)[1]; const {useDrumStore:dr}=await import(url);
 const tick=()=>new Promise(r=>setTimeout(r,60)); const checks=[];window.hostedPolyChecks=checks;window.hostedPolyError=null;
 const check=(ok,msg)=>{if(!ok)throw Error(msg);checks.push(msg)};
 seq.getState().enterFocus(0);seq.getState().setDeviceMode('seq');await tick();
 const focus=()=>seq.getState().focus; const clip=()=>seq.getState().session.scenes[focus().scene].clips[0];
 check(dr.getState().hosted,'Hosted engine attached');
 dr.getState().updateLaneRhythm(15,{enabled:true,steps:5});await tick();
 check(clip().drumRhythm.lanes[15].steps===5,'Metadata-only edit reaches owning clip');
 const firstScene=focus().scene;seq.getState().focusScene(firstScene===0?1:0);await tick();
 check(dr.getState().drumRhythm===undefined,'Focused clip loads its own complete rhythm');
 const previous=JSON.stringify(clip());dr.getState().undo();await tick();check(JSON.stringify(clip())===previous,'Focus change clears complete undo history');
 dr.getState().updateLaneRhythm(14,{enabled:false,sourceBar:0,steps:7});await tick();
 seq.getState().focusScene(firstScene);await tick();
 check(dr.getState().drumRhythm.lanes[15].steps===5&&!dr.getState().drumRhythm.lanes[14],'No cross-clip metadata leakage');
 dr.getState().updateLaneRhythm(15,{rotation:3});await tick();
 document.querySelector('.dr-lane-name').focus();
 document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'z',ctrlKey:true,bubbles:true}));await tick();
 check(clip().drumRhythm.lanes[15].rotation===0,'Hosted keyboard undo restores complete sequence');
 return checks;
})()
