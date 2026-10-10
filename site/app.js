'use strict';
const canvas = document.getElementById('canvas');
const status = document.getElementById('status');
const play = document.getElementById('play');
const audio = document.getElementById('audio');
const fullscreen = document.getElementById('fullscreen');
const restart = document.getElementById('restart');
let started = false;
let running = false;
let failed = false;
let sound = false;

function fail(message) {
  failed = true;
  running = false;
  status.textContent = 'Unable to run the game.';
  const error = document.getElementById('error');
  error.textContent = `${message} Use Restart to try again, or download the game below.`;
  error.hidden = false;
  restart.disabled = false;
  audio.disabled = true;
  fullscreen.disabled = true;
  play.disabled = true;
  play.textContent = 'Unable to start';
}

// The official release reads this global configuration before starting main().
var Module = {
  canvas,
  arguments: ['-prg', 'OTHELLO.PRG', '-run', '-ram', '512', '-scale', '1', '-quality', 'nearest', '-keymap', 'en-us'],
  locateFile: name => `emulator/${name}`,
  preRun: [function () {
    ENV.SDL_EMSCRIPTEN_KEYBOARD_ELEMENT = '#canvas';
    addRunDependency('othello-resources');
    const get = async path => {
      const response = await fetch(path, {signal: AbortSignal.timeout(30000)});
      if (!response.ok) throw new Error(`Could not download ${path}.`);
      return response;
    };
    get('game/manifest.json').then(response => response.json()).then(async manifest => {
      const resources = await Promise.all(manifest.resources.map(async name => {
        const response = await get(`game/${name}`);
        return [name, new Uint8Array(await response.arrayBuffer())];
      }));
      for (const [name, bytes] of resources) FS.writeFile(`/${name}`, bytes);
      removeRunDependency('othello-resources');
    }).catch(error => fail(error.message));
  }],
  postRun: [function () {
    if (failed) return;
    // SDL scales canvas coordinates into its native display itself. Its
    // document-level release listener also needs coordinates from the canvas.
    const fillMouse = fillMouseEventData;
    fillMouseEventData = (pointer, event, target) => {
      fillMouse(pointer, event, target);
      // SDL listens for mouseup on document so releases outside the canvas
      // still work. That event must use the same canvas coordinates too.
      if (event.type === 'mouseup' && target !== canvas) {
        const rect = canvas.getBoundingClientRect();
        HEAP32[(pointer >> 2) + 10] = Math.round(event.clientX - rect.left);
        HEAP32[(pointer >> 2) + 11] = Math.round(event.clientY - rect.top);
      }
    };
    document.getElementById('cover').hidden = true;
    canvas.hidden = false;
    running = true;
    status.textContent = 'Game running · press Enter to start.';
    audio.disabled = false;
    fullscreen.disabled = !document.getElementById('screen').requestFullscreen;
    restart.disabled = false;
    Module.ccall('j2c_start_audio', 'void', ['bool'], [false]);
    Module.SDL2?.audioContext?.suspend();
    canvas.focus({preventScroll: true});
  }],
  print: text => console.log(text),
  printErr: text => console.warn(text),
  setStatus: text => {
    if (text && !failed && !running) status.textContent = 'Loading the emulator…';
  },
  onAbort: () => fail('The emulator stopped unexpectedly.')
};

play.addEventListener('click', () => {
  if (started) return;
  if (typeof WebAssembly === 'undefined') return fail('This browser does not support WebAssembly.');
  started = true;
  play.disabled = true;
  play.textContent = 'Loading…';
  status.textContent = 'Loading the game and emulator…';
  const script = document.createElement('script');
  script.src = 'emulator/x16emu.js';
  script.onerror = () => fail('The emulator could not be downloaded.');
  document.body.append(script);
});
canvas.addEventListener('click', () => canvas.focus({preventScroll: true}));
canvas.addEventListener('keydown', event => {
  if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'Backspace'].includes(event.key)) event.preventDefault();
});
canvas.addEventListener('contextmenu', event => event.preventDefault());
canvas.addEventListener('webglcontextlost', event => {
  event.preventDefault();
  fail('The browser lost its graphics context.');
});
audio.addEventListener('click', async () => {
  audio.disabled = true;
  try {
    Module.ccall('j2c_start_audio', 'void', ['bool'], [!sound]);
    const context = Module.SDL2?.audioContext;
    if (!sound) {
      if (!context) throw new Error('Audio context unavailable');
      await context.resume();
    } else {
      await context?.suspend();
    }
    sound = !sound;
    audio.textContent = sound ? 'Sound on' : 'Sound off';
    audio.setAttribute('aria-pressed', String(sound));
    canvas.focus({preventScroll: true});
  } catch {
    Module.ccall('j2c_start_audio', 'void', ['bool'], [false]);
    sound = false;
    audio.textContent = 'Sound off';
    audio.setAttribute('aria-pressed', 'false');
    status.textContent = 'Audio is unavailable. You can keep playing.';
  } finally {
    audio.disabled = failed;
  }
});
fullscreen.addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await document.getElementById('screen').requestFullscreen();
    canvas.focus({preventScroll: true});
  } catch {
    status.textContent = 'Fullscreen is unavailable in this browser.';
  }
});
restart.addEventListener('click', () => location.reload());

// Align the ROM's initially centered pointer with SDL's initial host position.
// Give the KERNAL time to clamp each movement at the screen's edges.
let mouseAligned = false;
let mouseAligning = false;
canvas.addEventListener('mousemove', async event => {
  if (!running || !event.isTrusted || mouseAligned) return;
  event.stopImmediatePropagation();
  if (mouseAligning) return;
  mouseAligning = true;
  const x = event.clientX, y = event.clientY;
  const move = (clientX, clientY) => canvas.dispatchEvent(new MouseEvent('mousemove', {bubbles: true, clientX, clientY}));
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
  const rect = canvas.getBoundingClientRect();
  move(rect.left, rect.top);
  await wait(80);
  // Small packets avoid overflowing the r49 emulated mouse input buffer.
  // SDL starts at (0,0), while the ROM starts at the center of its screen.
  for (let step = 1; step <= 8; step++) {
    move(rect.left + rect.width * step / 8, rect.top + rect.height * step / 8);
    await wait(60);
  }
  for (let step = 7; step >= 0; step--) {
    move(rect.left + rect.width * step / 8, rect.top + rect.height * step / 8);
    await wait(60);
  }
  for (let step = 1; step <= 4; step++) {
    move(rect.left + (x - rect.left) * step / 4, rect.top + (y - rect.top) * step / 4);
    await wait(60);
  }
  mouseAligned = true;
  mouseAligning = false;
}, true);
for (const type of ['mousedown', 'mouseup']) canvas.addEventListener(type, event => {
  if (mouseAligning) event.stopImmediatePropagation();
}, true);
