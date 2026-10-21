// Executes the actual QML queue functions, not a second algorithm implementation.
const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const qml = fs.readFileSync(require('path').join(__dirname, '../qml/MusicPlayer.qml'), 'utf8');
function extract(name) {
  const start = qml.indexOf('function ' + name + '(');
  let cursor = qml.indexOf('{', start), depth = 1, end = cursor + 1;
  while (depth && end < qml.length) { if (qml[end] === '{') depth++; if (qml[end] === '}') depth--; end++; }
  return qml.slice(start, end);
}
const context = { playingQueue: [{id:'1'}, {id:'2'}, {id:'3'}], playingIndex: 0,
  playbackOrder: [], shufflePlayback: false, repeatMode:'off' };
context.root = context;
context.playQueueIndex = i => { context.playingIndex = i; context.selected = i; };
vm.createContext(context);
vm.runInContext(extract('rebuildPlaybackOrder') + '\n' + extract('navigatePlayback'), context);
context.rebuildPlaybackOrder();
context.navigatePlayback(1, false); assert.equal(context.selected, 1);
context.navigatePlayback(-1, false); assert.equal(context.selected, 0);
context.selected = null; context.navigatePlayback(-1, false); assert.equal(context.selected, null);
context.repeatMode = 'all'; context.navigatePlayback(-1, false); assert.equal(context.selected, 2);
context.navigatePlayback(1, true); assert.equal(context.selected, 0);
context.repeatMode = 'one'; context.navigatePlayback(1, true); assert.equal(context.selected, 0);
context.navigatePlayback(1, false); assert.equal(context.selected, 1);
context.shufflePlayback = true; context.rebuildPlaybackOrder();
assert.equal(context.playbackOrder[0], 1);
assert.equal(new Set(context.playbackOrder).size, 3);
context.navigatePlayback(1, false); assert.notEqual(context.selected, 1);
console.log('PASS queue navigation, boundaries, repeat-one, repeat-all, stable shuffle');

// Execute the production seek controller with a player that records call order.
const calls=[];
const player={seekable:true,duration:90000,playbackState:1,playbackRate:1,
  pause(){calls.push('pause');this.playbackState=2;},play(){calls.push('play');this.playbackState=1;},
  set position(v){calls.push(['position',v,this.playbackState]);}};
const timer=()=>({running:false,restart(){this.running=true;},stop(){this.running=false;}});
const seek={audioPlayer:player,MediaPlayer:{PlayingState:1},onlineLoading:false,onlinePlayback:false,
  cachedOnlineTrackId:'track-1',pendingOnlineId:'track-1',onlineSourceUrl:{toString:()=> 'https://audio.example/track-1.mp3'},
  seeking:false,seekCachePending:false,seekCacheTrackId:'',seekCacheSourceSwap:false,seekCachePositionApplied:false,
  seekPreviewPosition:0,seekCommitTimer:timer(),seekSettleTimer:timer(),seekCacheTimer:timer(),
  statusText:'',musicService:{cacheAudioForSeek:(id,url)=>calls.push(['cache',id,url]),cancelSeekAudioCache:()=>{}},
  I18n:{t:text=>text}};
seek.root=seek;
vm.createContext(seek);
vm.runInContext(['previewSeek','requestSeek','commitSeek','cancelSeek'].map(extract).join('\n'),seek);
assert.equal(seek.previewSeek(30000),true);
assert.equal(seek.seekCommitTimer.running,false); // drag preview has no commit timer
assert.deepEqual(calls,[]);
seek.requestSeek(40000);seek.requestSeek(45000);seek.commitSeek();
assert.deepEqual(calls,[['position',45000,1]]); // seek in place, never interrupt PLAYING
assert.equal(player.playbackRate,1);
assert.equal(player.playbackState,1);
assert.equal(seek.seekCommitTimer.running,false);
assert.equal(seek.seekSettleTimer.running,true);
calls.length=0;player.playbackState=2;player.playbackRate=2;
seek.requestSeek(-5);seek.commitSeek();
assert.deepEqual(calls,[['position',0,2]]); // preserve user's paused state
assert.equal(player.playbackRate,2); // seeking does not rewrite playback rate
calls.length=0;seek.requestSeek(999999);seek.commitSeek();
assert.deepEqual(calls,[['position',89999,2]]);
calls.length=0;seek.cancelSeek();seek.requestSeek(NaN);seek.requestSeek(Infinity);seek.commitSeek();
assert.deepEqual(calls,[]);
seek.requestSeek(20000);seek.cancelSeek();seek.commitSeek();assert.deepEqual(calls,[]);
player.seekable=false;assert.equal(seek.previewSeek(20000),false);
player.seekable=true;seek.onlineLoading=true;assert.equal(seek.previewSeek(20000),false);
console.log('PASS local seek clamping, paused-state preservation, invalid input and cancellation');

// Remote online tracks must be fully cached before any seek or source swap.
calls.length=0;player.playbackState=1;seek.onlineLoading=false;seek.onlinePlayback=true;seek.cachedOnlineTrackId='';
seek.requestSeek(52000);seek.commitSeek();
assert.deepEqual(calls,[['cache','track-1','https://audio.example/track-1.mp3']]);
assert.equal(seek.seekCachePending,true);
assert.equal(seek.seekCacheTrackId,'track-1');
assert.equal(seek.pendingCachedSeekPosition,52000);
assert.equal(player.playbackState,1); // Existing playing state is untouched while downloading.
assert.equal(seek.seekSettleTimer.running,false);
seek.requestSeek(64000);seek.commitSeek();
assert.deepEqual(calls,[['cache','track-1','https://audio.example/track-1.mp3'],['cache','track-1','https://audio.example/track-1.mp3']]);
assert.equal(seek.pendingCachedSeekPosition,64000); // latest target wins
seek.cancelSeek();
console.log('PASS remote seek waits for full-track cache, preserves source playback while downloading, and coalesces to latest target');
