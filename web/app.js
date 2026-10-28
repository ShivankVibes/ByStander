/**
 * ByStander v2.0 - Complete Web Controller & Dual-OLED Digital Twin
 * Real-time 128x32 dual canvas simulator, Web Audio 8-bit synth, Spotify, Ollie BLE & Pomodoro
 */

// ==========================================================================
// Web Audio Synthesizer (Retro 8-bit sound effects for ESP32 buzzer)
// ==========================================================================
class SoundSynth {
  constructor() {
    this.ctx = null;
    this.muted = false;
  }

  init() {
    if (!this.ctx) {
      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      if (AudioCtx) this.ctx = new AudioCtx();
    }
  }

  playTone(freq, type = 'sine', duration = 0.08, gainVal = 0.12) {
    if (this.muted) return;
    this.init();
    if (!this.ctx) return;
    if (this.ctx.state === 'suspended') this.ctx.resume();

    try {
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(freq, this.ctx.currentTime);

      gain.gain.setValueAtTime(gainVal, this.ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.0001, this.ctx.currentTime + duration);

      osc.connect(gain);
      gain.connect(this.ctx.destination);

      osc.start();
      osc.stop(this.ctx.currentTime + duration);
    } catch (e) {
      console.warn('Audio tone err:', e);
    }
  }

  btnClick() {
    this.playTone(880, 'square', 0.04, 0.08);
  }

  btnSelect() {
    this.playTone(660, 'sine', 0.06, 0.1);
    setTimeout(() => this.playTone(1320, 'sine', 0.08, 0.12), 60);
  }

  btnBack() {
    this.playTone(440, 'triangle', 0.07, 0.09);
  }

  reflexSuccess() {
    this.playTone(523.25, 'sine', 0.08, 0.15);
    setTimeout(() => this.playTone(659.25, 'sine', 0.08, 0.15), 80);
    setTimeout(() => this.playTone(783.99, 'sine', 0.15, 0.18), 160);
  }

  reflexFail() {
    this.playTone(220, 'sawtooth', 0.2, 0.15);
  }

  pomoAlarm() {
    [880, 1108, 1318, 1760].forEach((f, i) => {
      setTimeout(() => this.playTone(f, 'sine', 0.15, 0.15), i * 140);
    });
  }

  soundVictory() {
    this.reflexSuccess();
  }

  soundHappy() {
    this.btnSelect();
  }

  soundChirp() {
    this.btnClick();
  }

  soundPoke() {
    this.btnBack();
  }

  soundPowerup() {
    this.reflexSuccess();
  }
}

const synth = new SoundSynth();

// ==========================================================================
// Global State & Modes (Directly mirroring deskbuddy.ino)
// ==========================================================================
const MODES = {
  EYES: 0,
  POMODORO: 1,
  OLLIE: 2,
  REFLEX: 3,
  SPOTIFY: 4,
  SYSINFO: 5
};

const MENU_ITEMS = [
  "Companion Eyes",
  "Focus Pomodoro",
  "Ollie 2B-4FCD",
  "Reflex Speed Test",
  "Spotify Player",
  "System Status"
];

const state = {
  currentMode: MODES.EYES,
  menuActive: false,
  menuIndex: 0,
  
  // Expressions
  currentMood: 'neutral', // neutral, happy, look_left, look_right, surprised, wink, sleepy
  blinkProgress: 0, // 0 = open, 1 = fully closed
  lastBlinkTime: Date.now(),
  
  // Spotify
  spotify: {
    playing: true,
    trackIndex: 0,
    progressSec: 88,
    totalSec: 230,
    tracks: [
      { title: "Starboy", artist: "The Weeknd ft. Daft Punk", totalSec: 230, emoji: "⚡" },
      { title: "Blinding Lights", artist: "The Weeknd", totalSec: 200, emoji: "🕶️" },
      { title: "Midnight City", artist: "M83", totalSec: 243, emoji: "🌆" },
      { title: "Get Lucky", artist: "Daft Punk ft. Pharrell", totalSec: 248, emoji: "✨" },
      { title: "Instant Crush", artist: "Daft Punk ft. Julian", totalSec: 337, emoji: "🤖" }
    ],
    marqueeOffset: 0
  },

  // Ollie BLE
  ollie: {
    batteryPct: 88,
    voltage: 3.92,
    rssi: -64,
    colorName: "Cyan",
    colorHex: "#00e5ff",
    connected: true
  },

  // Pomodoro
  pomodoro: {
    duration: 1500, // 25 min default
    remaining: 1500,
    running: false,
    intervalId: null
  },

  // Reflex Game
  reflex: {
    phase: 'idle', // idle, waiting, ready, result
    startTime: 0,
    timeoutId: null,
    score: null,
    bestScore: 198
  }
};

// ==========================================================================
// Dual OLED Pixel Renderer (128x32 per screen, SSD1306 simulation)
// ==========================================================================
class DualOledRenderer {
  constructor() {
    this.canvasLeft = document.getElementById('oled-left');
    this.canvasRight = document.getElementById('oled-right');
    this.ctxLeft = this.canvasLeft.getContext('2d');
    this.ctxRight = this.canvasRight.getContext('2d');

    // Turn off smoothing for crisp OLED pixels
    this.ctxLeft.imageSmoothingEnabled = false;
    this.ctxRight.imageSmoothingEnabled = false;

    this.oledColor = '#00e5ff'; // Cyan pixel color
    this.oledDim = '#006677';
    this.oledBg = '#010810';
  }

  clear(ctx) {
    ctx.fillStyle = this.oledBg;
    ctx.fillRect(0, 0, 128, 32);
  }

  // Draw simulated 5x7 bitmap font
  drawText(ctx, text, x, y, size = 1, isInverse = false, color = this.oledColor) {
    ctx.save();
    ctx.font = `${size === 1 ? '9px' : '14px'} 'JetBrains Mono', monospace`;
    ctx.textBaseline = 'top';

    if (isInverse) {
      const width = ctx.measureText(text).width + 6;
      ctx.fillStyle = color;
      ctx.fillRect(x - 2, y - 1, width, size === 1 ? 11 : 16);
      ctx.fillStyle = this.oledBg;
      ctx.fillText(text, x + 1, y);
    } else {
      ctx.fillStyle = color;
      ctx.fillText(text, x, y);
    }
    ctx.restore();
  }

  // Draw eye pupil / shape
  drawEye(ctx, mood, isRightEye = false, blink = 0) {
    const cx = 64;
    const cy = 16;
    const baseW = 34;
    const baseH = 22;

    ctx.save();
    ctx.fillStyle = this.oledColor;
    ctx.strokeStyle = this.oledColor;
    ctx.lineWidth = 2;

    if (blink > 0.05) {
      // Eyelid closing animation
      const h = Math.max(2, baseH * (1 - blink));
      ctx.beginPath();
      ctx.ellipse(cx, cy, baseW / 2, h / 2, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
      return;
    }

    switch (mood) {
      case 'happy': {
        // Happy inverted arc / smile eye ^ ^
        ctx.beginPath();
        ctx.lineWidth = 3.5;
        ctx.arc(cx, cy + 6, 14, Math.PI * 1.15, Math.PI * 1.85, false);
        ctx.stroke();
        break;
      }

      case 'surprised': {
        // Wide circle O O
        ctx.beginPath();
        ctx.arc(cx, cy, 12, 0, Math.PI * 2);
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(cx, cy, 4, 0, Math.PI * 2);
        ctx.fill();
        break;
      }

      case 'wink': {
        if (isRightEye) {
          // Closed winking line on right eye
          ctx.lineWidth = 3;
          ctx.beginPath();
          ctx.moveTo(cx - 16, cy);
          ctx.lineTo(cx + 16, cy);
          ctx.stroke();
        } else {
          // Normal open rounded rectangle on left eye
          this.roundRect(ctx, cx - 18, cy - 10, 36, 20, 7);
          ctx.fill();
        }
        break;
      }

      case 'sleepy': {
        // Half-closed droopy eye
        this.roundRect(ctx, cx - 18, cy + 1, 36, 9, 3);
        ctx.fill();
        // Zzz indicator on right eye
        if (isRightEye) {
          this.drawText(ctx, "zZ", cx + 22, 4, 1);
        }
        break;
      }

      case 'look_left': {
        // Glancing left
        this.roundRect(ctx, cx - 18, cy - 10, 36, 20, 7);
        ctx.stroke();
        ctx.fillRect(cx - 14, cy - 6, 12, 12);
        break;
      }

      case 'look_right': {
        // Glancing right
        this.roundRect(ctx, cx - 18, cy - 10, 36, 20, 7);
        ctx.stroke();
        ctx.fillRect(cx + 2, cy - 6, 12, 12);
        break;
      }

      case 'neutral':
      default: {
        // Classic rounded neon eye with highlight
        this.roundRect(ctx, cx - 18, cy - 10, 36, 20, 6);
        ctx.fill();
        // Inner pupil pupil reflection cutout
        ctx.fillStyle = this.oledBg;
        ctx.fillRect(cx + 4, cy - 6, 4, 4);
        break;
      }
    }

    ctx.restore();
  }

  roundRect(ctx, x, y, width, height, radius) {
    ctx.beginPath();
    ctx.moveTo(x + radius, y);
    ctx.lineTo(x + width - radius, y);
    ctx.quadraticCurveTo(x + width, y, x + width, y + radius);
    ctx.lineTo(x + width, y + height - radius);
    ctx.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
    ctx.lineTo(x + radius, y + height);
    ctx.quadraticCurveTo(x, y + height, x, y + height - radius);
    ctx.lineTo(x, y + radius);
    ctx.quadraticCurveTo(x, y, x + radius, y);
    ctx.closePath();
  }

  // Draw Menu on Right OLED
  drawMenu(ctx) {
    this.drawText(ctx, " [ DESKBOT MENU ] ", 2, 2, 1, true);

    const sel = state.menuIndex;
    const item1 = `> ${MENU_ITEMS[sel]}`;
    const nextIdx = (sel + 1) % MENU_ITEMS.length;
    const item2 = `  ${MENU_ITEMS[nextIdx]}`;

    this.drawText(ctx, item1, 4, 13, 1, false);
    this.drawText(ctx, item2, 4, 23, 1, false, this.oledDim);
  }

  // Render Left Display
  renderLeft() {
    this.clear(this.ctxLeft);

    // Left display always shows Left Eye expression with periodic blinks!
    this.drawEye(this.ctxLeft, state.currentMood, false, state.blinkProgress);

    // Top small corner indicator
    this.drawText(this.ctxLeft, "DESK", 2, 2, 1, false, "#004455");
  }

  // Render Right Display based on mode / menu
  renderRight() {
    this.clear(this.ctxRight);

    if (state.menuActive) {
      this.drawMenu(this.ctxRight);
      return;
    }

    switch (state.currentMode) {
      case MODES.EYES: {
        // Option A: Right animated eye
        this.drawEye(this.ctxRight, state.currentMood, true, state.blinkProgress);
        this.drawText(this.ctxRight, "BOT", 104, 2, 1, false, "#004455");
        break;
      }

      case MODES.SPOTIFY: {
        const cur = state.spotify.tracks[state.spotify.trackIndex];
        // Header
        this.drawText(this.ctxRight, state.spotify.playing ? "▶ SPOTIFY" : "⏸ SPOTIFY", 4, 2, 1, true);

        // Equalizer bouncing bars
        const t = Date.now() / 120;
        for (let b = 0; b < 6; b++) {
          const h = state.spotify.playing ? (Math.sin(t + b * 1.2) * 5 + 6) : 2;
          this.ctxRight.fillStyle = this.oledColor;
          this.ctxRight.fillRect(88 + b * 6, 2 + (10 - h), 4, h);
        }

        // Marquee scrolling track title
        const fullTrackText = `${cur.title} - ${cur.artist}   `;
        state.spotify.marqueeOffset = (state.spotify.marqueeOffset + 0.5) % 180;
        this.drawText(this.ctxRight, fullTrackText, 4 - (state.spotify.marqueeOffset % 90), 13, 1);

        // Progress bar
        const pct = state.spotify.progressSec / cur.totalSec;
        this.ctxRight.strokeStyle = this.oledDim;
        this.ctxRight.strokeRect(4, 24, 88, 5);
        this.ctxRight.fillStyle = this.oledColor;
        this.ctxRight.fillRect(4, 24, Math.floor(88 * pct), 5);

        // Duration text
        const sec = Math.floor(state.spotify.progressSec % 60).toString().padStart(2, '0');
        const min = Math.floor(state.spotify.progressSec / 60);
        this.drawText(this.ctxRight, `${min}:${sec}`, 96, 22, 1);
        break;
      }

      case MODES.OLLIE: {
        this.drawText(this.ctxRight, "OLLIE 2B-4FCD", 4, 2, 1, true);

        // Battery gauge
        this.ctxRight.strokeRect(4, 14, 60, 9);
        this.ctxRight.fillRect(64, 16, 2, 5);
        const fillW = Math.floor((56 * state.ollie.batteryPct) / 100);
        this.ctxRight.fillRect(6, 16, fillW, 5);

        this.drawText(this.ctxRight, `${state.ollie.batteryPct}%`, 72, 13, 1);
        this.drawText(this.ctxRight, `${state.ollie.voltage}V • ${state.ollie.colorName}`, 4, 24, 1, false, this.oledDim);
        break;
      }

      case MODES.POMODORO: {
        this.drawText(this.ctxRight, "FOCUS POMO", 4, 2, 1, true);

        const m = Math.floor(state.pomodoro.remaining / 60).toString().padStart(2, '0');
        const s = (state.pomodoro.remaining % 60).toString().padStart(2, '0');
        this.drawText(this.ctxRight, `${m}:${s}`, 4, 14, 2);

        const tag = state.pomodoro.running ? "ACTIVE" : "PAUSED";
        this.drawText(this.ctxRight, `[${tag}]`, 76, 18, 1, false, state.pomodoro.running ? this.oledColor : this.oledDim);
        break;
      }

      case MODES.REFLEX: {
        this.drawText(this.ctxRight, "REFLEX SPEED", 4, 2, 1, true);

        if (state.reflex.phase === 'waiting') {
          this.drawText(this.ctxRight, "WAIT FOR IT...", 4, 15, 1);
        } else if (state.reflex.phase === 'ready') {
          this.drawText(this.ctxRight, "HIT BUTTON 2!", 4, 14, 1, true);
        } else if (state.reflex.phase === 'result') {
          this.drawText(this.ctxRight, `TIME: ${state.reflex.score} ms`, 4, 14, 1);
          this.drawText(this.ctxRight, `BEST: ${state.reflex.bestScore} ms`, 4, 24, 1, false, this.oledDim);
        } else {
          this.drawText(this.ctxRight, "PRESS B2 TO START", 4, 16, 1);
        }
        break;
      }

      case MODES.SYSINFO: {
        this.drawText(this.ctxRight, "SYS: ESP32-WROOM", 4, 2, 1, true);
        this.drawText(this.ctxRight, "Wire0+Wire1 OK", 4, 13, 1);
        this.drawText(this.ctxRight, "GPIO: 32,33,27", 4, 23, 1, false, this.oledDim);
        break;
      }
    }
  }

  render() {
    this.renderLeft();
    this.renderRight();
  }
}

const oled = new DualOledRenderer();

// ==========================================================================
// Animation & Blinking Loop
// ==========================================================================
function updateBlink() {
  const now = Date.now();
  const elapsed = now - state.lastBlinkTime;

  if (elapsed > 3500) {
    // 250ms quick blink animation
    const blinkPhase = elapsed - 3500;
    if (blinkPhase < 125) {
      state.blinkProgress = blinkPhase / 125;
    } else if (blinkPhase < 250) {
      state.blinkProgress = 1 - (blinkPhase - 125) / 125;
    } else {
      state.blinkProgress = 0;
      state.lastBlinkTime = now + Math.random() * 2000;
    }
  }
}

function mainLoop() {
  updateBlink();
  oled.render();
  requestAnimationFrame(mainLoop);
}

// ==========================================================================
// Hardware Buttons Navigation (Mirrors ESP32 GPIO 32, 33, 27)
// ==========================================================================
function pressHardwareButton(btnNum) {
  synth.init();
  const btnEl = document.getElementById(`hw-btn-${btnNum}`);
  if (btnEl) {
    btnEl.classList.add('pressed');
    setTimeout(() => btnEl.classList.remove('pressed'), 180);
  }

  if (btnNum === 1) {
    // Button 1: Menu / Next Item
    synth.btnClick();
    if (!state.menuActive) {
      state.menuActive = true;
      state.menuIndex = state.currentMode;
    } else {
      state.menuIndex = (state.menuIndex + 1) % MENU_ITEMS.length;
    }
    updateModeBadge();
  } else if (btnNum === 2) {
    // Button 2: Select / OK / Action
    synth.btnSelect();
    if (state.menuActive) {
      state.currentMode = state.menuIndex;
      state.menuActive = false;
      updateModeBadge();
    } else {
      // In-mode action
      handleModeAction();
    }
  } else if (btnNum === 3) {
    // Button 3: Back / Mood switch
    synth.btnBack();
    if (state.menuActive) {
      state.menuActive = false;
      updateModeBadge();
    } else if (state.currentMode !== MODES.EYES) {
      state.currentMode = MODES.EYES;
      updateModeBadge();
    } else {
      setMood(moods[nextIdx]);
    }
  }

  // Send button press command directly to ESP32
  fetch(`/api/button?btn=${btnNum}`).catch(() => {});
}

function handleModeAction() {
  if (state.currentMode === MODES.SPOTIFY) {
    toggleSpotifyPlay();
  } else if (state.currentMode === MODES.POMODORO) {
    togglePomodoro();
  } else if (state.currentMode === MODES.REFLEX) {
    triggerReflexClick();
  }
}

function updateModeBadge() {
  const badge = document.getElementById('current-mode-badge');
  if (state.menuActive) {
    badge.textContent = `MENU [${state.menuIndex + 1}/${MENU_ITEMS.length}]`;
    badge.style.borderColor = 'var(--accent-purple)';
    badge.style.color = 'var(--accent-purple)';
  } else {
    badge.textContent = `${MENU_ITEMS[state.currentMode].toUpperCase()} MODE`;
    badge.style.borderColor = 'var(--accent-cyan-glow)';
    badge.style.color = 'var(--accent-cyan)';
  }
}

// ==========================================================================
// Expression & Mood Controller
// ==========================================================================
function setMood(moodName) {
  state.currentMood = moodName;
  document.querySelectorAll('.chip').forEach(c => {
    c.classList.toggle('active', c.dataset.mood === moodName);
  });
  if (espBaseUrl) {
    fetch(`${espBaseUrl}/api/mood?set=${moodName}`).catch(() => {});
  }
}

document.querySelectorAll('.chip').forEach(chip => {
  chip.addEventListener('click', () => {
    synth.btnClick();
    setMood(chip.dataset.mood);
  });
});

// ==========================================================================
// Spotify Widget Logic
// ==========================================================================
const trackTitleEl = document.getElementById('web-track-title');
const trackArtistEl = document.getElementById('web-track-artist');
const trackProgressEl = document.getElementById('web-track-progress');
const timeCurEl = document.getElementById('time-current');
const timeTotEl = document.getElementById('time-total');
const btnPlayEl = document.getElementById('btn-play');
const vinylDiscEl = document.getElementById('vinyl-disc');

function updateSpotifyUI() {
  const cur = state.spotify.tracks[state.spotify.trackIndex];
  trackTitleEl.textContent = cur.title;
  trackArtistEl.textContent = cur.artist;

  const pct = (state.spotify.progressSec / (cur.totalSec || 180)) * 100;
  trackProgressEl.style.width = `${Math.min(100, Math.max(0, pct))}%`;

  const curM = Math.floor(state.spotify.progressSec / 60);
  const curS = Math.floor(state.spotify.progressSec % 60).toString().padStart(2, '0');
  const totM = Math.floor((cur.totalSec || 180) / 60);
  const totS = Math.floor((cur.totalSec || 180) % 60).toString().padStart(2, '0');

  timeCurEl.textContent = `${curM}:${curS}`;
  timeTotEl.textContent = `${totM}:${totS}`;

  btnPlayEl.textContent = state.spotify.playing ? '⏸' : '▶';
  vinylDiscEl.classList.toggle('paused', !state.spotify.playing);
}

// Live ESP32 & Spotify Status Synchronizer
async function syncSpotifyStatus() {
  try {
    const url = `${espBaseUrl || ''}/api/status`;
    const res = await fetch(url, { mode: 'cors' });
    if (res.ok) {
      const data = await res.json();
      state.currentMode = data.mode;
      if (data.spotify) {
        const sp = data.spotify;
        state.spotify.playing = sp.playing;
        state.spotify.progressSec = sp.progress;
        state.spotify.totalSec = sp.total > 0 ? sp.total : 180;

        const title = sp.track || "ByStander Groove";
        const artist = sp.artist || "ByStander Audio";

        trackTitleEl.textContent = title;
        trackArtistEl.textContent = artist;

        // Keep OLED simulator track in sync with physical hardware
        state.spotify.tracks[state.spotify.trackIndex].title = title;
        state.spotify.tracks[state.spotify.trackIndex].artist = artist;
        state.spotify.tracks[state.spotify.trackIndex].totalSec = state.spotify.totalSec;

        const pct = (state.spotify.progressSec / state.spotify.totalSec) * 100;
        trackProgressEl.style.width = `${Math.min(100, Math.max(0, pct))}%`;

        const curM = Math.floor(state.spotify.progressSec / 60);
        const curS = Math.floor(state.spotify.progressSec % 60).toString().padStart(2, '0');
        const totM = Math.floor(state.spotify.totalSec / 60);
        const totS = Math.floor(state.spotify.totalSec % 60).toString().padStart(2, '0');

        timeCurEl.textContent = `${curM}:${curS}`;
        timeTotEl.textContent = `${totM}:${totS}`;

        btnPlayEl.textContent = sp.playing ? '⏸' : '▶';
        vinylDiscEl.classList.toggle('paused', !sp.playing);

        const liveTag = document.querySelector('.live-tag');
        if (liveTag) {
          liveTag.textContent = sp.apiActive ? 'SPOTIFY CLOUD LIVE' : 'ESP32 TRACK';
          liveTag.style.background = sp.apiActive ? 'rgba(29, 185, 84, 0.25)' : 'rgba(0, 240, 255, 0.2)';
        }
      }

      if (data.ollie) {
        state.ollie.batteryPct = data.ollie.battery;
        olliePctEl.textContent = `${data.ollie.battery}%`;
        ollieFluidEl.style.width = `${data.ollie.battery}%`;
      }

      updateModeBadge();
    }
  } catch (err) {}
}

// Poll ESP32 status every 1200ms right from startup
setInterval(syncSpotifyStatus, 1200);

async function toggleSpotifyPlay() {
  synth.btnClick();
  const nextAction = state.spotify.playing ? 'pause' : 'play';
  try {
    await fetch(`${espBaseUrl || ''}/api/spotify?action=${nextAction}`);
    setTimeout(syncSpotifyStatus, 200);
  } catch (e) {}
  state.spotify.playing = !state.spotify.playing;
  btnPlayEl.textContent = state.spotify.playing ? '⏸' : '▶';
  vinylDiscEl.classList.toggle('paused', !state.spotify.playing);
}

btnPlayEl.addEventListener('click', toggleSpotifyPlay);

document.getElementById('btn-next').addEventListener('click', async () => {
  synth.btnClick();
  try {
    await fetch(`${espBaseUrl || ''}/api/spotify?action=next`);
    setTimeout(syncSpotifyStatus, 250);
  } catch (e) {}
});

document.getElementById('btn-prev').addEventListener('click', async () => {
  synth.btnClick();
  try {
    await fetch(`${espBaseUrl || ''}/api/spotify?action=prev`);
    setTimeout(syncSpotifyStatus, 250);
  } catch (e) {}
});

// Smooth second-by-second progress interpolation between polls
setInterval(() => {
  if (state.spotify.playing && state.spotify.totalSec > 0) {
    state.spotify.progressSec += 1;
    if (state.spotify.progressSec > state.spotify.totalSec) {
      state.spotify.progressSec = state.spotify.totalSec;
    }
    const pct = (state.spotify.progressSec / state.spotify.totalSec) * 100;
    trackProgressEl.style.width = `${Math.min(100, Math.max(0, pct))}%`;

    const curM = Math.floor(state.spotify.progressSec / 60);
    const curS = Math.floor(state.spotify.progressSec % 60).toString().padStart(2, '0');
    timeCurEl.textContent = `${curM}:${curS}`;
  }
}, 1000);

// ==========================================================================
// Sphero Ollie BLE Controller Logic (2-Axis Pilot, Precision cm & Tricks)
// ==========================================================================
const ollieFluidEl = document.getElementById('ollie-fluid');
const olliePctEl = document.getElementById('ollie-pct-label');
const statusOlliePill = document.getElementById('status-ollie');

// ==========================================================================
// Cloud MQTT Bridge Configuration (24/7 HTTPS Web Sync via EMQX Public WSS)
// ==========================================================================
const MQTT_BROKER_URL = 'wss://broker.emqx.io:8084/mqtt';
const MQTT_DEVICE_ID = '2b4fcd';
const MQTT_TOPIC_CMD = `bystander/${MQTT_DEVICE_ID}/cmd`;
const MQTT_TOPIC_STATUS = `bystander/${MQTT_DEVICE_ID}/status`;

let mqttClient = null;
let isMqttConnected = false;

function initMqttBridge() {
  if (typeof mqtt === 'undefined') return;

  const cloudPill = document.getElementById('status-cloud');
  const cloudText = document.getElementById('cloud-status-text');
  const cloudDot = document.getElementById('cloud-dot');
  const clientId = 'bystander_web_' + Math.random().toString(16).substr(2, 8);

  try {
    mqttClient = mqtt.connect(MQTT_BROKER_URL, {
      clientId: clientId,
      clean: true,
      connectTimeout: 4000,
      reconnectPeriod: 3000
    });

    mqttClient.on('connect', () => {
      isMqttConnected = true;
      console.log('[MQTT] Connected to EMQX Cloud Broker via WSS!');
      if (cloudText) cloudText.textContent = 'Cloud: Live (24/7)';
      if (cloudPill) cloudPill.classList.add('connected');
      if (cloudDot) {
        cloudDot.className = 'status-dot pulse-purple';
      }
      mqttClient.subscribe(MQTT_TOPIC_STATUS);
    });

    mqttClient.on('message', (topic, payload) => {
      try {
        const data = JSON.parse(payload.toString());
        if (topic === MQTT_TOPIC_STATUS) {
          if (data.mode !== undefined) {
            state.mode = data.mode;
            updateModeBadge();
          }
          if (data.ollie) {
            if (data.ollie.battery !== undefined) state.ollie.battery = data.ollie.battery;
            if (data.ollie.connected !== undefined) state.ollie.connected = data.ollie.connected;
            updateOllieUI();
          }
          if (data.wifi && data.wifi.ip) {
            state.wifi.ip = data.wifi.ip;
            state.wifi.connected = true;
            updateWifiUI();
          }
        }
      } catch (e) {
        console.warn('[MQTT] Error parsing status payload:', e);
      }
    });

    mqttClient.on('error', (err) => {
      console.warn('[MQTT] Connection notice:', err);
    });

    mqttClient.on('offline', () => {
      isMqttConnected = false;
      if (cloudText) cloudText.textContent = 'Cloud: Offline';
      if (cloudPill) cloudPill.classList.remove('connected');
      if (cloudDot) cloudDot.className = 'status-dot';
    });
  } catch (err) {
    console.warn('[MQTT] Initialization error:', err);
  }
}

// Send API Command to ESP32 for Ollie (via Cloud MQTT or Local HTTP)
async function sendOllieCommand(params) {
  if (isMqttConnected && mqttClient) {
    mqttClient.publish(MQTT_TOPIC_CMD, JSON.stringify(params));
    return { status: "ok" };
  }
  const query = new URLSearchParams(params).toString();
  try {
    const res = await fetch(`${espBaseUrl || ''}/api/ollie?${query}`);
    return await res.json();
  } catch (err) {
    return null;
  }
}

// 1. Color Palette Swatches
document.querySelectorAll('.color-swatch').forEach(swatch => {
  swatch.addEventListener('click', () => {
    synth.btnClick();
    document.querySelectorAll('.color-swatch').forEach(s => s.classList.remove('selected'));
    swatch.classList.add('selected');

    state.ollie.colorName = swatch.dataset.color;
    state.ollie.colorHex = swatch.dataset.hex;

    // Send color update to Ollie
    sendOllieCommand({ action: 'color', name: state.ollie.colorName });

    // Visual feedback
    if (statusOlliePill) {
      statusOlliePill.querySelector('.status-dot').style.backgroundColor = state.ollie.colorHex;
      statusOlliePill.querySelector('.status-dot').style.boxShadow = `0 0 10px ${state.ollie.colorHex}`;
    }
  });
});

// 2. Interactive 2-Axis Joystick Pilot
const joyBase = document.getElementById('ollie-joystick-base');
const joyKnob = document.getElementById('ollie-joystick-knob');
const joyHeadingVal = document.getElementById('joy-heading-val');
const joySpeedVal = document.getElementById('joy-speed-val');

if (joyBase && joyKnob) {
  let isPiloting = false;
  let joyCenter = { x: 0, y: 0 };
  const maxRadius = 45; // Max knob travel distance in pixels
  let lastJoySendTime = 0;

  function updateJoystickPosition(clientX, clientY) {
    const dx = clientX - joyCenter.x;
    const dy = clientY - joyCenter.y;
    const dist = Math.hypot(dx, dy);
    const clampedDist = Math.min(dist, maxRadius);
    
    // Angle: 0 rad is Up/North, PI/2 is Right/East
    const angleRad = Math.atan2(dx, -dy);
    const knobX = Math.sin(angleRad) * clampedDist;
    const knobY = -Math.cos(angleRad) * clampedDist;

    joyKnob.style.transform = `translate(${knobX}px, ${knobY}px)`;

    const headingDeg = Math.round((angleRad * (180 / Math.PI) + 360) % 360);
    const throttlePct = Math.round((clampedDist / maxRadius) * 100);

    // Direction name helper
    let dirName = 'N';
    if (headingDeg >= 23 && headingDeg < 68) dirName = 'NE';
    else if (headingDeg >= 68 && headingDeg < 113) dirName = 'E';
    else if (headingDeg >= 113 && headingDeg < 158) dirName = 'SE';
    else if (headingDeg >= 158 && headingDeg < 203) dirName = 'S';
    else if (headingDeg >= 203 && headingDeg < 248) dirName = 'SW';
    else if (headingDeg >= 248 && headingDeg < 293) dirName = 'W';
    else if (headingDeg >= 293 && headingDeg < 338) dirName = 'NW';

    if (joyHeadingVal) joyHeadingVal.textContent = `${headingDeg}° (${dirName})`;
    if (joySpeedVal) joySpeedVal.textContent = `${throttlePct}%`;

    // Stream drive packets to Ollie (throttled every 70ms)
    const now = Date.now();
    if (now - lastJoySendTime >= 70) {
      lastJoySendTime = now;
      const speedScaled = Math.round((throttlePct / 100) * 140); // speed range 0-140
      sendOllieCommand({ action: 'roll', speed: speedScaled, heading: headingDeg });
    }
  }

  joyBase.addEventListener('pointerdown', (e) => {
    isPiloting = true;
    joyBase.setPointerCapture(e.pointerId);
    const rect = joyBase.getBoundingClientRect();
    joyCenter = {
      x: rect.left + rect.width / 2,
      y: rect.top + rect.height / 2
    };
    updateJoystickPosition(e.clientX, e.clientY);
  });

  joyBase.addEventListener('pointermove', (e) => {
    if (!isPiloting) return;
    updateJoystickPosition(e.clientX, e.clientY);
  });

  function releaseJoystick(e) {
    if (!isPiloting) return;
    isPiloting = false;
    joyKnob.style.transform = 'translate(0px, 0px)';
    if (joyHeadingVal) joyHeadingVal.textContent = '0° (Idle)';
    if (joySpeedVal) joySpeedVal.textContent = '0%';
    // Immediate brake packet
    sendOllieCommand({ action: 'stop' });
  }

  joyBase.addEventListener('pointerup', releaseJoystick);
  joyBase.addEventListener('pointercancel', releaseJoystick);
}

// 3. Precision Distance Controller (in cm)
const ollieCmInput = document.getElementById('ollie-cm-val');
const btnCmMinus = document.getElementById('btn-cm-minus');
const btnCmPlus = document.getElementById('btn-cm-plus');
const lblFwdCm = document.getElementById('lbl-fwd-cm');
const lblBackCm = document.getElementById('lbl-back-cm');
const btnDriveFwd = document.getElementById('btn-drive-fwd');
const btnDriveBack = document.getElementById('btn-drive-back');
const btnOllieSpin = document.getElementById('btn-ollie-spin');
const btnOllieBrake = document.getElementById('btn-ollie-brake');

function updateDistanceLabels(val) {
  const clamped = Math.max(5, Math.min(200, val));
  if (ollieCmInput) ollieCmInput.value = clamped;
  if (lblFwdCm) lblFwdCm.textContent = `${clamped} cm`;
  if (lblBackCm) lblBackCm.textContent = `${clamped} cm`;

  // Update active chip state
  document.querySelectorAll('.cm-chip').forEach(chip => {
    chip.classList.toggle('active', parseInt(chip.dataset.cm, 10) === clamped);
  });
}

if (btnCmMinus && ollieCmInput) {
  btnCmMinus.addEventListener('click', () => {
    synth.btnClick();
    const curr = parseInt(ollieCmInput.value, 10) || 20;
    updateDistanceLabels(curr - 5);
  });
}

if (btnCmPlus && ollieCmInput) {
  btnCmPlus.addEventListener('click', () => {
    synth.btnClick();
    const curr = parseInt(ollieCmInput.value, 10) || 20;
    updateDistanceLabels(curr + 5);
  });
}

if (ollieCmInput) {
  ollieCmInput.addEventListener('input', () => {
    const val = parseInt(ollieCmInput.value, 10);
    if (!isNaN(val)) updateDistanceLabels(val);
  });
}

document.querySelectorAll('.cm-chip').forEach(chip => {
  chip.addEventListener('click', () => {
    synth.btnClick();
    const cm = parseInt(chip.dataset.cm, 10);
    updateDistanceLabels(cm);
  });
});

if (btnDriveFwd) {
  btnDriveFwd.addEventListener('click', () => {
    synth.soundChirp();
    const cm = parseInt(ollieCmInput ? ollieCmInput.value : 20, 10) || 20;
    sendOllieCommand({ action: 'move_cm', distance: cm, heading: 0, direction: 'forward' });
  });
}

if (btnDriveBack) {
  btnDriveBack.addEventListener('click', () => {
    synth.soundChirp();
    const cm = parseInt(ollieCmInput ? ollieCmInput.value : 20, 10) || 20;
    sendOllieCommand({ action: 'move_cm', distance: cm, heading: 180, direction: 'backward' });
  });
}

if (btnOllieSpin) {
  btnOllieSpin.addEventListener('click', () => {
    synth.soundVictory();
    sendOllieCommand({ action: 'spin' });
  });
}

if (btnOllieBrake) {
  btnOllieBrake.addEventListener('click', () => {
    synth.soundPoke();
    sendOllieCommand({ action: 'stop' });
  });
}

// ==========================================================================
// Pomodoro Focus Timer Logic
// ==========================================================================
const pomoDigitsEl = document.getElementById('web-timer-display');
const pomoSubEl = document.getElementById('pomo-status-sub');
const pomoRingEl = document.getElementById('pomo-ring');
const btnPomoToggle = document.getElementById('btn-pomo-toggle');
const btnPomoReset = document.getElementById('btn-pomo-reset');

function updatePomodoroUI() {
  const m = Math.floor(state.pomodoro.remaining / 60).toString().padStart(2, '0');
  const s = (state.pomodoro.remaining % 60).toString().padStart(2, '0');
  pomoDigitsEl.textContent = `${m}:${s}`;

  const circumference = 314.159; // 2 * PI * 50
  const progress = state.pomodoro.remaining / state.pomodoro.duration;
  const offset = circumference * (1 - progress);
  pomoRingEl.style.strokeDashoffset = offset;

  btnPomoToggle.textContent = state.pomodoro.running ? "Pause Timer" : "Start Timer";
  pomoSubEl.textContent = state.pomodoro.running ? "IN SESSION" : "PAUSED";
}

function togglePomodoro() {
  synth.btnClick();
  if (state.pomodoro.running) {
    clearInterval(state.pomodoro.intervalId);
    state.pomodoro.running = false;
  } else {
    state.pomodoro.running = true;
    state.pomodoro.intervalId = setInterval(() => {
      if (state.pomodoro.remaining > 0) {
        state.pomodoro.remaining--;
        updatePomodoroUI();
      } else {
        clearInterval(state.pomodoro.intervalId);
        state.pomodoro.running = false;
        synth.pomoAlarm();
        updatePomodoroUI();
      }
    }, 1000);
  }
  updatePomodoroUI();
}

btnPomoToggle.addEventListener('click', togglePomodoro);

btnPomoReset.addEventListener('click', () => {
  synth.btnClick();
  clearInterval(state.pomodoro.intervalId);
  state.pomodoro.running = false;
  state.pomodoro.remaining = state.pomodoro.duration;
  updatePomodoroUI();
});

document.querySelectorAll('.pomo-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    synth.btnClick();
    document.querySelectorAll('.pomo-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');

    state.pomodoro.duration = parseInt(btn.dataset.time, 10);
    state.pomodoro.remaining = state.pomodoro.duration;
    if (state.pomodoro.running) {
      clearInterval(state.pomodoro.intervalId);
      state.pomodoro.running = false;
    }
    updatePomodoroUI();
  });
});

// ==========================================================================
// Reflex Speed Minigame Logic
// ==========================================================================
const reflexPad = document.getElementById('reflex-pad');
const reflexMsg = document.getElementById('reflex-msg');
const reflexScore = document.getElementById('reflex-score-display');
const bestScoreLabel = document.getElementById('best-score-label');

function triggerReflexClick() {
  if (state.reflex.phase === 'idle' || state.reflex.phase === 'result') {
    // Start countdown
    synth.btnClick();
    state.reflex.phase = 'waiting';
    reflexPad.className = 'reflex-pad-area waiting';
    reflexMsg.textContent = 'WAIT FOR GREEN...';
    reflexScore.textContent = '';

    const delay = 1500 + Math.random() * 2500;
    state.reflex.timeoutId = setTimeout(() => {
      state.reflex.phase = 'ready';
      state.reflex.startTime = Date.now();
      reflexPad.className = 'reflex-pad-area ready';
      reflexMsg.textContent = '⚡ CLICK NOW!';
      synth.playTone(880, 'triangle', 0.05, 0.2);
    }, delay);

  } else if (state.reflex.phase === 'waiting') {
    // Clicked too early!
    clearTimeout(state.reflex.timeoutId);
    synth.reflexFail();
    state.reflex.phase = 'result';
    reflexPad.className = 'reflex-pad-area';
    reflexMsg.textContent = 'TOO EARLY! Click to retry';
    reflexScore.textContent = 'Penalty: +200ms';

  } else if (state.reflex.phase === 'ready') {
    // Success!
    const diff = Date.now() - state.reflex.startTime;
    synth.reflexSuccess();
    state.reflex.score = diff;
    state.reflex.phase = 'result';
    reflexPad.className = 'reflex-pad-area';
    reflexMsg.textContent = 'GREAT REFLEXES!';
    reflexScore.textContent = `${diff} ms`;

    if (diff < state.reflex.bestScore) {
      state.reflex.bestScore = diff;
      bestScoreLabel.textContent = `${diff} ms`;
    }
  }
}

reflexPad.addEventListener('click', triggerReflexClick);

// ==========================================================================
// Hardware Button Clicks & Keyboard Listeners (<kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd>)
// ==========================================================================
document.getElementById('hw-btn-1').addEventListener('click', () => pressHardwareButton(1));
document.getElementById('hw-btn-2').addEventListener('click', () => pressHardwareButton(2));
document.getElementById('hw-btn-3').addEventListener('click', () => pressHardwareButton(3));

window.addEventListener('keydown', (e) => {
  if (e.key === '1') {
    e.preventDefault();
    pressHardwareButton(1);
  } else if (e.key === '2') {
    e.preventDefault();
    pressHardwareButton(2);
  } else if (e.key === '3') {
    e.preventDefault();
    pressHardwareButton(3);
  }
});

// ==========================================================================
// WebSerial API Hardware Sync
// ==========================================================================
const btnWebSerial = document.getElementById('btn-webserial');
const statusEspPill = document.getElementById('status-esp32');

btnWebSerial.addEventListener('click', async () => {
  synth.btnClick();
  if ('serial' in navigator) {
    try {
      const port = await navigator.serial.requestPort();
      await port.open({ baudRate: 115200 });
      statusEspPill.querySelector('.status-label').textContent = 'ESP32: Live USB Connected';
      btnWebSerial.querySelector('span').textContent = 'Live Connected';
      btnWebSerial.style.borderColor = 'var(--accent-green)';
      btnWebSerial.style.color = 'var(--accent-green)';
    } catch (err) {
      console.warn('WebSerial error or cancelled:', err);
    }
  } else {
    alert("WebSerial is supported on Chrome/Edge on Desktop. Your browser twin is already running in high-fidelity simulation mode!");
  }
});

// ==========================================================================
// WiFi & Spotify Settings Modal + REST Synchronization
// ==========================================================================
let espBaseUrl = (window.location.protocol.startsWith('http') && window.location.hostname !== 'localhost' && window.location.hostname !== '127.0.0.1') ? window.location.origin : 'http://192.168.4.1';
let espPollingInterval = null;

const modalSettings = document.getElementById('modal-settings');
const btnOpenSettings = document.getElementById('btn-open-settings');
const btnCloseSettings = document.getElementById('btn-close-settings');
const btnConnectIp = document.getElementById('btn-connect-ip');
const inputEspIp = document.getElementById('cfg-esp-ip');
const ipFeedback = document.getElementById('ip-connection-feedback');
const btnSaveSpotify = document.getElementById('btn-save-spotify');
const btnSubmitSpotCode = document.getElementById('btn-submit-spot-code');
const inputSpotCode = document.getElementById('cfg-spot-code');
const spotFeedback = document.getElementById('spotify-auth-feedback');

btnOpenSettings.addEventListener('click', () => {
  synth.btnClick();
  modalSettings.classList.add('open');
});

btnCloseSettings.addEventListener('click', () => {
  synth.btnClick();
  modalSettings.classList.remove('open');
});

modalSettings.addEventListener('click', (e) => {
  if (e.target === modalSettings) {
    modalSettings.classList.remove('open');
  }
});

async function connectToEsp(targetIp) {
  let ip = (targetIp || 'http://192.168.4.1').trim();
  if (!ip) ip = 'http://192.168.4.1';
  if (!ip.startsWith('http')) ip = 'http://' + ip;

  if (ipFeedback) ipFeedback.textContent = 'Connecting to ESP32 at ' + ip + '...';
  try {
    const res = await fetch(`${ip}/api/status`, { mode: 'cors' });
    if (res.ok) {
      espBaseUrl = ip;
      localStorage.setItem('bystander_esp_ip', ip);
      if (ipFeedback) ipFeedback.textContent = 'Connected! Syncing live with ESP32.';
      statusEspPill.querySelector('.status-label').textContent = `ESP32: ${ip.replace('http://', '')} (WiFi)`;
      statusEspPill.querySelector('.status-dot').style.backgroundColor = 'var(--accent-green)';
      syncSpotifyStatus();
      return true;
    } else {
      if (ipFeedback) ipFeedback.textContent = 'Error: ESP32 returned status ' + res.status;
    }
  } catch (err) {
    if (ipFeedback) ipFeedback.textContent = 'Could not reach ESP32 at ' + ip + '. Connect to ByStander-WiFi or same router!';
  }
  return false;
}

btnConnectIp.addEventListener('click', async () => {
  synth.btnClick();
  const ip = inputEspIp.value.trim() || 'http://192.168.4.1';
  await connectToEsp(ip);
});

// Auto-connect to ESP32 IP on startup (defaulting to 192.168.4.1)
const savedEspIp = localStorage.getItem('bystander_esp_ip') || 'http://192.168.4.1';
if (inputEspIp) inputEspIp.value = savedEspIp.replace('http://', '');
connectToEsp(savedEspIp);

// 1-Click Multi-User Spotify Account Code Linker
if (btnSubmitSpotCode) {
  btnSubmitSpotCode.addEventListener('click', async () => {
    synth.btnClick();
    const raw = inputSpotCode.value.trim();
    if (!raw) {
      if (spotFeedback) {
        spotFeedback.textContent = 'Please paste the redirected link or code from Spotify!';
        spotFeedback.style.color = '#ff5252';
      }
      return;
    }
    if (spotFeedback) {
      spotFeedback.textContent = 'Authorizing with Spotify... Please wait.';
      spotFeedback.style.color = 'var(--accent-cyan)';
    }
    try {
      const url = `${espBaseUrl || ''}/api/spotify-auth?code=${encodeURIComponent(raw)}`;
      const res = await fetch(url);
      const data = await res.json();
      if (data.success) {
        if (spotFeedback) {
          spotFeedback.textContent = '✅ Spotify account connected! Live tracks synced.';
          spotFeedback.style.color = 'var(--accent-green)';
        }
        synth.soundVictory();
        setTimeout(() => {
          modalSettings.classList.remove('open');
          syncSpotifyStatus();
        }, 1600);
      } else {
        if (spotFeedback) {
          spotFeedback.textContent = '❌ ' + (data.message || 'Authorization failed. Check your WiFi connection & code.');
          spotFeedback.style.color = '#ff5252';
        }
      }
    } catch (e) {
      if (spotFeedback) {
        spotFeedback.textContent = '❌ Could not contact ESP32 to authorize.';
        spotFeedback.style.color = '#ff5252';
      }
    }
  });
}

btnSaveSpotify.addEventListener('click', async () => {
  synth.btnClick();
  const cId = document.getElementById('cfg-spot-id').value.trim();
  const cSec = document.getElementById('cfg-spot-secret').value.trim();
  const rTok = document.getElementById('cfg-spot-refresh').value.trim();

  try {
    await fetch(`${espBaseUrl || ''}/api/config?client_id=${encodeURIComponent(cId)}&client_secret=${encodeURIComponent(cSec)}&refresh_token=${encodeURIComponent(rTok)}`);
    alert("Custom Spotify credentials saved to ESP32 Flash!");
    modalSettings.classList.remove('open');
    setTimeout(syncSpotifyStatus, 500);
  } catch (e) {
    alert("Could not reach ESP32 to save config. Is it connected?");
  }
});

// WiFi Setup Handler (Connect to School / Home / Hotspot)
const btnSaveWifi = document.getElementById('btn-save-wifi');
const inputWifiSsid = document.getElementById('cfg-wifi-ssid');
const inputWifiPass = document.getElementById('cfg-wifi-pass');
const wifiFeedback = document.getElementById('wifi-setup-feedback');

if (btnSaveWifi) {
  btnSaveWifi.addEventListener('click', async () => {
    synth.btnClick();
    const ssid = inputWifiSsid.value.trim();
    const pass = inputWifiPass.value.trim();
    if (!ssid) {
      if (wifiFeedback) {
        wifiFeedback.textContent = 'Please enter a WiFi SSID/Network Name!';
        wifiFeedback.style.color = '#ff5252';
      }
      return;
    }
    if (wifiFeedback) {
      wifiFeedback.textContent = `Connecting ByStander to "${ssid}"... Check Right OLED!`;
      wifiFeedback.style.color = 'var(--accent-cyan)';
    }
    try {
      const url = `${espBaseUrl || ''}/api/wifi-setup?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`;
      await fetch(url, { headers: { 'X-Requested-With': 'XMLHttpRequest' } });
      if (wifiFeedback) {
        wifiFeedback.textContent = `✅ Saved "${ssid}" to Flash! Connecting now...`;
        wifiFeedback.style.color = 'var(--accent-green)';
      }
      synth.soundVictory();
    } catch (e) {
      if (wifiFeedback) {
        wifiFeedback.textContent = `✅ Credentials sent! ByStander is joining "${ssid}".`;
        wifiFeedback.style.color = 'var(--accent-green)';
      }
    }
  });
}

// ==========================================================================
// ByStander Voice Assistant & Speech Controller ("Hey ByStander")
// ==========================================================================
const voiceOrbBtn = document.getElementById('voice-orb-btn');
const btnVoiceToggle = document.getElementById('btn-voice-toggle');
const voiceBadge = document.getElementById('voice-badge');
const voiceUserText = document.getElementById('voice-user-text');
const voiceBotReply = document.getElementById('voice-bot-reply');

let isListening = false;
let recognition = null;

let isSpeaking = false;
let lastVoiceCmdTime = 0;

// Speech Synthesis Robot Voice
function speakByStander(text) {
  if (!('speechSynthesis' in window)) return;
  isSpeaking = true;
  // Instantly close microphone while robot speaks to prevent acoustic feedback loop!
  if (recognition) {
    try { recognition.abort(); } catch(e){}
  }
  window.speechSynthesis.cancel();
  const utterance = new SpeechSynthesisUtterance(text);
  utterance.pitch = 1.15; // Natural friendly robot pitch, no distortion/gibberish
  utterance.rate = 1.0;
  const voices = window.speechSynthesis.getVoices();
  const enVoice = voices.find(v => v.lang.startsWith('en') && (v.name.includes('Samantha') || v.name.includes('Google') || v.name.includes('Natural') || v.name.includes('Daniel')));
  if (enVoice) utterance.voice = enVoice;

  utterance.onend = () => {
    // 800ms cooldown window after speaking before mic can activate again
    setTimeout(() => {
      isSpeaking = false;
      stopListening();
    }, 800);
  };
  utterance.onerror = () => {
    isSpeaking = false;
    stopListening();
  };

  window.speechSynthesis.speak(utterance);
}

// Fun Robot Responses & Jokes
const BOT_JOKES = [
  "Why did the robot cross the road? Because it was programmed by a chicken!",
  "What is a robot's favorite type of music? Heavy metal!",
  "Why was the computer cold? It left its Windows open!",
  "There are 10 types of people in the world: those who understand binary, and those who don't!",
  "Why do robots never get tired? Because they have endless drive!"
];

// Process Voice Commands
function processVoiceCommand(rawTranscript) {
  const t = rawTranscript.toLowerCase().trim();
  voiceUserText.textContent = `"${rawTranscript}"`;

  // 1. Music / Spotify Commands
  if (t.includes('play') || t.includes('resume') || t.includes('unpause')) {
    toggleSpotifyPlay();
    synth.soundVictory();
    const reply = "Playing Spotify music!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('pause') || t.includes('stop music') || t.includes('hold on')) {
    toggleSpotifyPlay();
    synth.btnClick();
    const reply = "Music paused.";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('next') || t.includes('skip')) {
    document.getElementById('btn-next').click();
    synth.soundChirp();
    const reply = "Skipped to next track!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }

  // 2. Expressions & Eyes
  if (t.includes('happy') || t.includes('smile')) {
    setMood('happy');
    synth.soundHappy();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mood?set=happy`).catch(()=>{});
    const reply = "Happy to see you!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('wink')) {
    setMood('wink');
    synth.soundHappy();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mood?set=wink`).catch(()=>{});
    const reply = "Winking at you!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('surprised') || t.includes('shock') || t.includes('whoa')) {
    setMood('surprised');
    synth.soundPoke();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mood?set=surprised`).catch(()=>{});
    const reply = "Whoa! That caught me off guard!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('sleep') || t.includes('tired')) {
    setMood('sleepy');
    synth.btnClick();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mood?set=sleepy`).catch(()=>{});
    const reply = "Taking a power nap... zzz";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('neutral') || t.includes('normal')) {
    setMood('neutral');
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mood?set=neutral`).catch(()=>{});
    const reply = "Neutral gaze restored.";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }

  // 3. Sphero Ollie Motion & Precision Centimeter Commands
  // e.g. "move forward 20 cm", "drive back 15 cm", "roll 50 centimeters", "360 spin trick", "turn green"
  if (t.includes('move') || t.includes('drive') || t.includes('roll') || t.includes('spin') || t.includes('trick') || (t.includes('cm') && (t.includes('forward') || t.includes('back')))) {
    // 360 Spin Trick
    if (t.includes('spin') || t.includes('trick') || t.includes('360')) {
      sendOllieCommand({ action: 'spin' });
      synth.soundVictory();
      const reply = "Executing 360-degree spin trick on Ollie!";
      voiceBotReply.textContent = `"${reply}"`;
      speakByStander(reply);
      return;
    }

    // Stop / Brake
    if (t.includes('stop') || t.includes('brake') || t.includes('halt')) {
      sendOllieCommand({ action: 'stop' });
      synth.soundPoke();
      const reply = "Braking Ollie immediately!";
      voiceBotReply.textContent = `"${reply}"`;
      speakByStander(reply);
      return;
    }

    // Extract distance in cm
    const cmMatch = t.match(/(\d+)\s*(?:cm|centimeter|centimeters)/i) || 
                    t.match(/(?:forward|back|backward)\s*(?:by\s*)?(\d+)/i) ||
                    t.match(/(\d+)/);
    
    let distanceCm = 20; // Default 20 cm
    if (cmMatch && cmMatch[1]) {
      const parsed = parseInt(cmMatch[1], 10);
      if (!isNaN(parsed) && parsed > 0) distanceCm = parsed;
    }
    distanceCm = Math.max(5, Math.min(200, distanceCm)); // Safe clamp

    let heading = 0;
    let dirName = 'forward';
    if (t.includes('back') || t.includes('reverse') || t.includes('backward')) {
      heading = 180;
      dirName = 'backward';
    }

    // Trigger Ollie precision roll
    sendOllieCommand({ action: 'move_cm', distance: distanceCm, heading: heading });
    synth.soundChirp();
    const reply = `Rolling Ollie ${dirName} ${distanceCm} centimeters!`;
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }

  // Ollie Color Commands ("turn red", "set color cyan", "glow purple")
  if (t.includes('cyan') || t.includes('purple') || t.includes('green') || t.includes('red') || t.includes('yellow') || t.includes('blue')) {
    let col = "Cyan";
    if (t.includes('purple')) col = "Purple";
    else if (t.includes('green')) col = "Green";
    else if (t.includes('red')) col = "Red";
    else if (t.includes('yellow')) col = "Yellow";
    else if (t.includes('blue')) col = "Cyan";

    sendOllieCommand({ action: 'color', name: col });
    synth.soundHappy();
    const reply = `Set Ollie light to ${col}!`;
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }

  // 4. Modes
  if (t.includes('pomodoro') || t.includes('focus') || t.includes('timer')) {
    state.currentMode = MODES.POMODORO;
    updateModeBadge();
    togglePomodoro();
    synth.soundPowerup();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mode?set=1`).catch(()=>{});
    const reply = "Pomodoro focus timer started! 25 minutes on the clock.";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('ollie') || t.includes('battery')) {
    state.currentMode = MODES.OLLIE;
    updateModeBadge();
    synth.soundChirp();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mode?set=2`).catch(()=>{});
    const reply = `Sphero Ollie connected! Battery is at 88%.`;
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('reflex') || t.includes('game')) {
    state.currentMode = MODES.REFLEX;
    updateModeBadge();
    synth.soundChirp();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mode?set=3`).catch(()=>{});
    const reply = "Reflex reaction game activated! Get ready!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('eyes') || t.includes('companion') || t.includes('home')) {
    state.currentMode = MODES.EYES;
    updateModeBadge();
    synth.soundHappy();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mode?set=0`).catch(()=>{});
    const reply = "Dual companion eyes active!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }

  // 4. Alexa-style Dumb Q&A and Jokes
  if (t.includes('joke')) {
    const joke = BOT_JOKES[Math.floor(Math.random() * BOT_JOKES.length)];
    voiceBotReply.textContent = `"${joke}"`;
    synth.soundHappy();
    speakByStander(joke);
    return;
  }
  if (t.includes('who are you') || t.includes('what is bystander')) {
    const reply = "I am ByStander, your dual-OLED robotic companion and productivity buddy!";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('hello') || t.includes('hi') || t.includes('hey')) {
    setMood('happy');
    synth.soundHappy();
    const reply = "Hello human! What can I do for you today?";
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('time')) {
    const now = new Date();
    const reply = `The time is ${now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}.`;
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }
  if (t.includes('status') || t.includes('system') || t.includes('ip')) {
    state.currentMode = MODES.STATUS;
    updateModeBadge();
    if (espBaseUrl) fetch(`${espBaseUrl}/api/mode?set=5`).catch(()=>{});
    const reply = `All systems online. ESP32 linked. Spotify synced.`;
    voiceBotReply.textContent = `"${reply}"`;
    speakByStander(reply);
    return;
  }

  // Fallback
  synth.soundPoke();
  const fallback = `I heard "${rawTranscript}". Try 'play music', 'happy face', or 'start timer'!`;
  voiceBotReply.textContent = `"${fallback}"`;
  speakByStander("Command received.");
}

// Setup Web Speech Recognition
const SpeechRec = window.SpeechRecognition || window.webkitSpeechRecognition;

if (SpeechRec) {
  recognition = new SpeechRec();
  recognition.continuous = false;
  recognition.interimResults = true; // Stream words in real-time as user speaks!
  recognition.lang = 'en-US';

  let finalPhrase = '';

  recognition.onstart = () => {
    isListening = true;
    finalPhrase = '';
    voiceBadge.textContent = 'LISTENING...';
    voiceBadge.classList.add('listening');
    voiceOrbBtn.classList.add('listening');
    btnVoiceToggle.classList.add('listening');
    voiceUserText.textContent = "Listening to your voice...";
  };

  recognition.onresult = (e) => {
    if (isSpeaking) return; // Drop audio captured while robot was speaking

    let interimPhrase = '';
    for (let i = e.resultIndex; i < e.results.length; ++i) {
      const res = e.results[i];
      if (res.isFinal) {
        finalPhrase += res[0].transcript;
      } else {
        interimPhrase += res[0].transcript;
      }
    }

    // Display active speech in real time
    const displayWords = (finalPhrase + ' ' + interimPhrase).trim();
    if (displayWords) {
      voiceUserText.textContent = `"${displayWords}"`;
    }

    // Process ONLY when sentence is completed & marked final!
    if (finalPhrase.trim()) {
      const fullSentence = finalPhrase.trim();
      finalPhrase = '';
      
      const now = Date.now();
      if (now - lastVoiceCmdTime > 1000) {
        lastVoiceCmdTime = now;
        processVoiceCommand(fullSentence);
      }
      stopListening();
    }
  };

  recognition.onerror = (e) => {
    if (e.error === 'not-allowed') {
      voiceUserText.textContent = `🔒 Mic permission not allowed on HTTP. Tap the input box below and speak using your keyboard's 🎙️ icon!`;
      const txtIn = document.getElementById('voice-text-input');
      if (txtIn) txtIn.focus();
    } else if (e.error !== 'no-speech') {
      voiceUserText.textContent = `Microphone notice: ${e.error}. Try clicking a quick command below!`;
    }
    stopListening();
  };

  recognition.onend = () => {
    if (finalPhrase.trim()) {
      const fullSentence = finalPhrase.trim();
      finalPhrase = '';
      const now = Date.now();
      if (now - lastVoiceCmdTime > 1000) {
        lastVoiceCmdTime = now;
        processVoiceCommand(fullSentence);
      }
    }
    stopListening();
  };
}

function startListening() {
  const txtIn = document.getElementById('voice-text-input');
  // Check if browser blocks SpeechRecognition on insecure HTTP context
  if (!window.isSecureContext && location.hostname !== 'localhost' && location.hostname !== '127.0.0.1') {
    voiceBadge.textContent = "USE KEYBOARD 🎙️";
    voiceUserText.textContent = "🔒 Browser mic requires HTTPS. Tap the box below and use your keyboard's 🎙️ microphone, or tap a chip!";
    if (txtIn) txtIn.focus();
    return;
  }

  if (!recognition) {
    voiceUserText.textContent = "Speech recognition unavailable. Tap the box below and use your keyboard 🎙️ mic!";
    if (txtIn) txtIn.focus();
    return;
  }
  try {
    recognition.start();
  } catch (e) {
    voiceUserText.textContent = "🔒 Mic blocked by browser. Tap the box below and use your keyboard 🎙️ mic!";
    if (txtIn) txtIn.focus();
    try { recognition.stop(); } catch(err){}
  }
}

function stopListening() {
  isListening = false;
  voiceBadge.textContent = "READY (PRESS 'V')";
  voiceBadge.classList.remove('listening');
  voiceOrbBtn.classList.remove('listening');
  btnVoiceToggle.classList.remove('listening');
}

function toggleListening() {
  synth.btnClick();
  if (isListening) {
    if (recognition) { try { recognition.stop(); } catch(e){} }
    stopListening();
  } else {
    startListening();
  }
}

voiceOrbBtn.addEventListener('click', toggleListening);
btnVoiceToggle.addEventListener('click', () => {
  document.getElementById('voice-assistant-card').scrollIntoView({ behavior: 'smooth' });
  toggleListening();
});

// Manual Command Bar (Keyboard Mic / Text Input)
const voiceTextInput = document.getElementById('voice-text-input');
const voiceSendBtn = document.getElementById('voice-send-btn');

function handleManualVoiceSubmit() {
  if (!voiceTextInput) return;
  const val = voiceTextInput.value.trim();
  if (val.length === 0) return;
  synth.btnClick();
  voiceUserText.textContent = `"${val}"`;
  processVoiceCommand(val);
  voiceTextInput.value = '';
}

if (voiceSendBtn && voiceTextInput) {
  voiceSendBtn.addEventListener('click', handleManualVoiceSubmit);
  voiceTextInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      handleManualVoiceSubmit();
    }
  });
}

// Quick command chips
document.querySelectorAll('.v-chip').forEach(chip => {
  chip.addEventListener('click', () => {
    synth.btnClick();
    const cmd = chip.dataset.cmd;
    processVoiceCommand(cmd);
  });
});

// Keyboard shortcut: Press 'V' to toggle voice recognition
window.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
  if (e.key === 'v' || e.key === 'V') {
    toggleListening();
  }
});

// ==========================================================================
// Start Main Loop & Initialization
// ==========================================================================
updateSpotifyUI();
updatePomodoroUI();
updateModeBadge();
initMqttBridge();
requestAnimationFrame(mainLoop);

