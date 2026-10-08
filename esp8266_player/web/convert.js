// "Convert MIDI" card: choose instruments and settings for one .mid file, preview it as a
// piano roll, listen to it in the browser, then hand it to the upload code in app.js.
// The conversion itself is StepperConverter (converter.js). Entry point: openConverter(file).
(function () {
  'use strict';

  const byId = id => document.getElementById(id);

  // Tunable values are in settings.js (SETTINGS.convert, SETTINGS.choices).
  const MOTOR_COLORS = SETTINGS.convert.motorColors;
  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const LISTEN_VOLUME = SETTINGS.convert.listenVolume;
  const LISTEN_SCHEDULE_AHEAD_S = SETTINGS.convert.listenScheduleAheadS;
  const LISTEN_SCHEDULE_EVERY_MS = SETTINGS.convert.listenScheduleEveryMs;

  let openFile = null;    // { midi, parts, resolve } while the card is open
  let conversion = null;  // latest StepperConverter.convert() result for the current settings
  let listening = null;   // { audioContext, timer } while Listen plays

  // MIDI note number -> name, e.g. 60 -> "C4"
  function noteName(note) {
    return NOTE_NAMES[note % 12] + (Math.floor(note / 12) - 1);
  }

  function formatTime(ms) {
    const seconds = Math.floor(ms / 1000);
    return Math.floor(seconds / 60) + ':' + String(seconds % 60).padStart(2, '0');
  }

  // The card's settings as StepperConverter.convert() options.
  function readOptions() {
    const checked = document.querySelectorAll('#convertParts input:checked');
    const [low, high] = byId('convertRange').value.split(',').map(Number);  // "-1,-1" = off
    return {
      mode: byId('convertMotorMode').value,
      keep: new Set([...checked].map(checkbox => checkbox.value)),
      speed: Number(byId('convertSpeed').value) / 100,
      transpose: Number(byId('convertTranspose').value),
      low: low >= 0 ? low : null,
      high: low >= 0 ? high : null,
    };
  }

  // Track name, plus the instrument when it adds something (or tells same-named tracks apart).
  function partLabel(part, parts) {
    const nameIsShared = parts.filter(other => other.name === part.name).length > 1;
    let label = part.name;
    if (part.instrument && (nameIsShared || part.instrument !== part.name)) label += ' · ' + part.instrument;
    return label;
  }

  // One checkbox per instrument (track + channel). Drums are off by default: they are not pitched.
  function renderParts() {
    const box = byId('convertParts');
    box.textContent = '';
    for (const part of openFile.parts) {
      const row = document.createElement('label');
      row.className = 'convert-part';
      const checkbox = document.createElement('input');
      checkbox.type = 'checkbox';
      checkbox.value = part.key;
      checkbox.checked = !part.drums;
      checkbox.onchange = updatePreview;
      const name = document.createElement('span');
      name.textContent = partLabel(part, openFile.parts);
      const details = document.createElement('span');
      details.className = 'muted';
      details.textContent = `ch ${part.channel + 1} · ${part.notes} notes · ${noteName(part.low)}–${noteName(part.high)}`
        + (part.drums ? ' · drums (not pitched)' : '');
      row.append(checkbox, name, details);
      box.appendChild(row);
    }
  }

  // Converts again with the current settings and refreshes the summary and piano roll.
  function updatePreview() {
    stopListening();
    conversion = StepperConverter.convert(openFile.midi, readOptions());
    const summary = StepperConverter.summarize(conversion);
    const stats = conversion.stats;
    const hasNotes = conversion.events.length > 0;
    byId('convertSummary').textContent = hasNotes
      ? `${summary.notes} notes · ${formatTime(summary.lengthMs)} · up to ${stats.maxAtOnce || 1} at once`
        + (stats.stolen ? ` · ${stats.stolen} notes cut short (more than 5 at once)` : ' · fits the 5 motors')
        + ` · per motor: ${summary.perMotor.join(' / ')}`
      : 'No notes selected.';
    byId('convertAddButton').disabled = !hasNotes;
    byId('convertListenButton').disabled = !hasNotes;
    drawPianoRoll();
  }

  // Piano roll: time left to right, pitch bottom to top, one colour per motor.
  function drawPianoRoll() {
    const canvas = byId('convertPianoRoll');
    const width = canvas.width = canvas.clientWidth * devicePixelRatio;
    const height = canvas.height = canvas.clientHeight * devicePixelRatio;
    const draw = canvas.getContext('2d');
    draw.clearRect(0, 0, width, height);
    const events = conversion.events;  // [timeMs, motor, note, durationMs]
    if (!events.length) return;
    let endMs = 0, lowest = 127, highest = 0;
    for (const [time, , note, duration] of events) {
      endMs = Math.max(endMs, time + duration);
      lowest = Math.min(lowest, note);
      highest = Math.max(highest, note);
    }
    const rowHeight = Math.max(1, height / Math.max(1, highest - lowest + 1));
    for (const [time, motor, note, duration] of events) {
      draw.fillStyle = MOTOR_COLORS[motor] || '#888';
      draw.fillRect(time / endMs * width, height - (note - lowest + 1) * rowHeight,
        Math.max(1, duration / endMs * width), Math.max(1, rowHeight - 0.5));
    }
  }

  // Listen in the browser: one square-wave voice per note. Notes are scheduled a few seconds
  // ahead in small batches, so a long song does not create thousands of oscillators at once.
  function toggleListening() {
    if (listening) {
      stopListening();
      return;
    }
    const audioContext = new (window.AudioContext || window.webkitAudioContext)();
    const volume = audioContext.createGain();
    volume.gain.value = LISTEN_VOLUME;
    volume.connect(audioContext.destination);
    const events = conversion.events.slice();
    // Stop after the note that ENDS last (the note that starts last can end earlier).
    const songEndS = events.reduce((end, [time, , , duration]) => Math.max(end, time + duration), 0) / 1000;
    const startS = audioContext.currentTime + 0.1;
    let nextIndex = 0;
    const scheduleMore = () => {
      const playedS = audioContext.currentTime - startS;
      while (nextIndex < events.length && events[nextIndex][0] / 1000 < playedS + LISTEN_SCHEDULE_AHEAD_S) {
        const [time, , note, duration] = events[nextIndex++];
        const oscillator = audioContext.createOscillator();
        oscillator.type = 'square';
        oscillator.frequency.value = 440 * Math.pow(2, (note - 69) / 12);
        oscillator.connect(volume);
        oscillator.start(startS + time / 1000);
        oscillator.stop(startS + (time + duration) / 1000);
      }
      if (nextIndex >= events.length && playedS > songEndS) stopListening();
    };
    scheduleMore();
    listening = { audioContext, timer: setInterval(scheduleMore, LISTEN_SCHEDULE_EVERY_MS) };
    byId('convertListenButton').textContent = 'Stop listening';
  }

  function stopListening() {
    if (!listening) return;
    clearInterval(listening.timer);
    listening.audioContext.close();
    listening = null;
    byId('convertListenButton').textContent = 'Listen';
  }

  // Hides the card and answers the waiting openConverter() promise.
  function closeCard(uploadOrNull) {
    stopListening();
    byId('convertPanel').hidden = true;
    const resolve = openFile.resolve;
    openFile = null;
    resolve(uploadOrNull);
  }

  function addToPlayer() {
    const name = byId('convertSongName').value.trim() || 'song';
    const summary = StepperConverter.summarize(conversion);
    closeCard({
      blob: new Blob([StepperConverter.toStepperBytes(conversion.events)]),
      name,
      info: ' (' + summary.notes + ' notes, ' + formatTime(summary.lengthMs) + ')',
    });
  }

  function setAllParts(checked) {
    document.querySelectorAll('#convertParts input').forEach(checkbox => { checkbox.checked = checked; });
    updatePreview();
  }

  // Opens the card for one MIDI file. Resolves with { blob, name, info } to upload, or with
  // null when cancelled; rejects when the file cannot be read.
  window.openConverter = function (file) {
    return file.arrayBuffer().then(buffer => new Promise((resolve, reject) => {
      let midi;
      try {
        midi = StepperConverter.parseMidi(buffer);
      } catch (error) {
        reject(error);
        return;
      }
      const parts = StepperConverter.listParts(midi);
      if (!parts.length) {
        reject(new Error('no notes found in this MIDI file'));
        return;
      }
      openFile = { midi, parts, resolve };
      // Song names on the player: up to 23 characters, letters, digits, space and _()-
      byId('convertSongName').value = file.name.replace(/\.[^.]*$/, '').replace(/[^A-Za-z0-9 _()-]/g, '_').slice(0, 23);
      byId('convertFileName').textContent = file.name;
      renderParts();
      byId('convertPanel').hidden = false;
      byId('convertPanel').scrollIntoView({ behavior: 'smooth', block: 'start' });
      updatePreview();
    }));
  };

  // This script is loaded at the end of <body>, so the card's elements already exist.
  ['convertMotorMode', 'convertSpeed', 'convertTranspose', 'convertRange'].forEach(id => {
    byId(id).onchange = updatePreview;
  });
  byId('convertSpeed').oninput = () => {  // live percentage while dragging; converts on release
    byId('convertSpeedValue').textContent = byId('convertSpeed').value + '%';
  };
  byId('convertSelectAll').onclick = event => {
    event.preventDefault();
    setAllParts(true);
  };
  byId('convertSelectNone').onclick = event => {
    event.preventDefault();
    setAllParts(false);
  };
  byId('convertListenButton').onclick = toggleListening;
  byId('convertCancelButton').onclick = () => closeCard(null);
  byId('convertAddButton').onclick = addToPlayer;
  window.addEventListener('resize', () => {
    if (openFile) drawPianoRoll();
  });
})();
