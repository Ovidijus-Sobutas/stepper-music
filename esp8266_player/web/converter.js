// MIDI -> .stepper converter, running in the browser (used by convert.js).
// A faithful port of midi_to_stepper.py (stepper_music project), including the parts of the
// Python 'mido' library it relies on (file reading, merge_tracks), so that with default options
// the output is byte-identical to the Python script's. Check any change with
// tools/converter_test.html.
//
// .stepper file format (little-endian):
//   header, 12 bytes: "STPM", version (1), 3 reserved bytes, uint32 event count
//   per event, 8 bytes: uint32 start time ms, uint8 motor, uint8 MIDI note, uint16 duration ms
//
// Public API (window.StepperConverter): parseMidi, listParts, convert, toStepperBytes, summarize.
(function (global) {
  'use strict';

  const NUM_MOTORS = 5;
  const NOTES_PER_MOTOR = Math.floor(128 / NUM_MOTORS);  // 25, for the "by pitch" assignment
  const DEFAULT_TEMPO_US = 500000;                       // microseconds per beat = 120 BPM
  const MIN_NOTE_MS = 10;                                // shorter notes are lengthened to this

  // ======================================================================= MIDI file reading (as mido)

  // Reads big-endian numbers and MIDI variable-length numbers from a byte array.
  class ByteReader {
    constructor(bytes) {
      this.bytes = bytes;
      this.pos = 0;
    }

    atEnd() {
      return this.pos >= this.bytes.length;
    }

    readByte() {
      if (this.pos >= this.bytes.length) throw new Error('Unexpected end of file');
      return this.bytes[this.pos++];
    }

    readUint16() {
      return (this.readByte() << 8) | this.readByte();
    }

    readUint32() {
      // ">>> 0" keeps the top byte from making the number negative
      return ((this.readByte() << 24) >>> 0) + (this.readByte() << 16) + (this.readByte() << 8) + this.readByte();
    }

    readString(length) {
      let text = '';
      for (let i = 0; i < length; i++) text += String.fromCharCode(this.readByte());
      return text;
    }

    // 7 bits per byte, high bit set = more bytes follow (mido read_variable_int)
    readVarInt() {
      let value = 0;
      for (;;) {
        const byte = this.readByte();
        value = (value << 7) | (byte & 0x7f);
        if (!(byte & 0x80)) return value;
      }
    }
  }

  // Bytes in a channel or system message, status byte included (mido's message specs).
  const SYSTEM_MESSAGE_LENGTHS = { 0xf1: 2, 0xf2: 3, 0xf3: 2, 0xf6: 1, 0xf8: 1, 0xfa: 1, 0xfb: 1, 0xfc: 1, 0xfe: 1, 0xff: 1 };

  function messageLength(status) {
    if (status < 0xf0) {
      const kind = status & 0xf0;
      return (kind === 0xc0 || kind === 0xd0) ? 2 : 3;  // program change / channel pressure: 1 data byte
    }
    if (status in SYSTEM_MESSAGE_LENGTHS) return SYSTEM_MESSAGE_LENGTHS[status];
    throw new Error('Undefined status byte 0x' + status.toString(16));
  }

  // Latin-1 text of track and instrument names (as mido).
  function decodeName(bytes) {
    return new TextDecoder('latin1').decode(bytes);
  }

  // One track chunk ending at byte position `end`. Keeps what the converter needs (notes, tempo,
  // program, names) and keeps every other message as a placeholder with its delta time, because
  // skipped messages still advance the clock. Each message: { time: delta ticks, type, ... }.
  function readTrack(reader, end) {
    const messages = [];
    let runningStatus = null;
    while (reader.pos < end) {
      const time = reader.readVarInt();
      let status = reader.readByte();
      let firstDataByte = [];
      if (status < 0x80) {
        // Running status: the status byte was left out, this is already the first data byte.
        if (runningStatus === null) throw new Error('Running status without last status');
        firstDataByte = [status];
        status = runningStatus;
      } else if (status !== 0xff) {
        runningStatus = status;  // meta messages do not set running status (mido)
      }

      if (status === 0xff) {  // meta message
        const metaType = reader.readByte();
        const length = reader.readVarInt();
        const data = reader.bytes.subarray(reader.pos, reader.pos + length);
        reader.pos += length;
        if (metaType === 0x51 && length >= 3) {
          messages.push({ time, type: 'set_tempo', tempo: (data[0] << 16) | (data[1] << 8) | data[2] });
        } else if (metaType === 0x03) {
          messages.push({ time, type: 'track_name', name: decodeName(data) });
        } else if (metaType === 0x04) {
          messages.push({ time, type: 'instrument_name', name: decodeName(data) });
        } else {
          messages.push({ time, type: 'meta' });
        }
      } else if (status === 0xf0 || status === 0xf7) {  // system exclusive
        const length = reader.readVarInt();
        reader.pos += length;
        messages.push({ time, type: 'sysex' });
      } else {  // channel or system message
        const data = firstDataByte.slice();
        const remaining = messageLength(status) - 1 - firstDataByte.length;
        for (let i = 0; i < remaining; i++) data.push(reader.readByte());
        for (const byte of data) {
          if (byte > 127) throw new Error('Data byte must be 0..127');
        }
        const kind = status & 0xf0;
        const channel = status & 0x0f;
        if (kind === 0x90) messages.push({ time, type: 'note_on', channel, note: data[0], velocity: data[1] });
        else if (kind === 0x80) messages.push({ time, type: 'note_off', channel, note: data[0], velocity: data[1] });
        else if (kind === 0xc0) messages.push({ time, type: 'program_change', channel, program: data[0] });
        else messages.push({ time, type: 'other' });
      }
    }
    reader.pos = end;
    return messages;
  }

  // ArrayBuffer of a .mid file -> { ticksPerBeat, tracks: [[message]] }
  function parseMidi(buffer) {
    const reader = new ByteReader(new Uint8Array(buffer));
    if (reader.readString(4) !== 'MThd') throw new Error('Not a MIDI file');
    const headerLength = reader.readUint32();
    reader.readUint16();  // format (0, 1 or 2): all are read the same way
    const trackCount = reader.readUint16();
    const division = reader.readUint16();
    reader.pos = 8 + headerLength;
    if (division & 0x8000) throw new Error('SMPTE time code MIDI files are not supported');
    const tracks = [];
    while (tracks.length < trackCount && !reader.atEnd()) {
      const chunkType = reader.readString(4);
      const chunkSize = reader.readUint32();
      const chunkEnd = Math.min(reader.pos + chunkSize, reader.bytes.length);
      if (chunkType !== 'MTrk') {  // skip unknown chunks
        reader.pos = chunkEnd;
        continue;
      }
      tracks.push(readTrack(reader, chunkEnd));
    }
    return { ticksPerBeat: division, tracks };
  }

  // mido.merge_tracks: all messages of all tracks with absolute times ("abs", in ticks) and
  // their track index, in track order, then stable-sorted by time.
  function mergeTracks(tracks) {
    const all = [];
    tracks.forEach((track, trackIndex) => {
      let now = 0;
      for (const message of track) {
        now += message.time;
        all.push(Object.assign({}, message, { abs: now, track: trackIndex }));
      }
    });
    all.sort((a, b) => a.abs - b.abs);  // Array.prototype.sort is stable
    return all;
  }

  // ======================================================================= timing (as midi_to_stepper.py)

  // [[start tick, microseconds per beat], ...] sorted by tick, starting at tick 0.
  // Of several tempo changes on the same tick the last one wins.
  function buildTempoMap(merged) {
    const changes = [];
    for (const message of merged) {
      if (message.type === 'set_tempo') changes.push([message.abs, message.tempo]);
    }
    changes.sort((a, b) => a[0] - b[0]);
    if (!changes.length) return [[0, DEFAULT_TEMPO_US]];
    const tempoMap = [];
    for (const [tick, tempoUs] of changes) {
      const last = tempoMap[tempoMap.length - 1];
      if (last && last[0] === tick) tempoMap[tempoMap.length - 1] = [tick, tempoUs];
      else tempoMap.push([tick, tempoUs]);
    }
    if (tempoMap[0][0] > 0) tempoMap.unshift([0, DEFAULT_TEMPO_US]);
    return tempoMap;
  }

  // Absolute tick -> milliseconds (rounded down), adding up each tempo segment before it.
  function tickToMs(tick, ticksPerBeat, tempoMap) {
    if (ticksPerBeat <= 0) return 0;
    let totalUs = 0;
    for (let i = 0; i < tempoMap.length; i++) {
      const [segmentStart, tempoUs] = tempoMap[i];
      if (tick <= segmentStart) break;
      const nextStart = i + 1 < tempoMap.length ? tempoMap[i + 1][0] : tick;
      const segmentEnd = Math.min(tick, nextStart);
      if (segmentEnd > segmentStart) totalUs += (segmentEnd - segmentStart) * tempoUs / ticksPerBeat;
      if (segmentEnd === tick) break;
    }
    return Math.trunc(totalUs / 1000);
  }

  // ======================================================================= instruments

  // General MIDI program names (program number = index).
  const GM_INSTRUMENTS = ['Acoustic Grand Piano','Bright Piano','Electric Grand','Honky-tonk Piano','Electric Piano 1','Electric Piano 2',
    'Harpsichord','Clavinet','Celesta','Glockenspiel','Music Box','Vibraphone','Marimba','Xylophone','Tubular Bells','Dulcimer',
    'Drawbar Organ','Percussive Organ','Rock Organ','Church Organ','Reed Organ','Accordion','Harmonica','Tango Accordion',
    'Nylon Guitar','Steel Guitar','Jazz Guitar','Clean Guitar','Muted Guitar','Overdrive Guitar','Distortion Guitar',
    'Guitar Harmonics','Acoustic Bass','Finger Bass','Pick Bass','Fretless Bass','Slap Bass 1','Slap Bass 2','Synth Bass 1',
    'Synth Bass 2','Violin','Viola','Cello','Contrabass','Tremolo Strings','Pizzicato Strings','Harp','Timpani',
    'String Ensemble 1','String Ensemble 2','Synth Strings 1','Synth Strings 2','Choir Aahs','Voice Oohs','Synth Voice',
    'Orchestra Hit','Trumpet','Trombone','Tuba','Muted Trumpet','French Horn','Brass Section','Synth Brass 1','Synth Brass 2',
    'Soprano Sax','Alto Sax','Tenor Sax','Baritone Sax','Oboe','English Horn','Bassoon','Clarinet','Piccolo','Flute',
    'Recorder','Pan Flute','Blown Bottle','Shakuhachi','Whistle','Ocarina','Square Lead','Saw Lead','Calliope Lead',
    'Chiff Lead','Charang Lead','Voice Lead','Fifths Lead','Bass + Lead','New Age Pad','Warm Pad','Polysynth Pad','Choir Pad',
    'Bowed Pad','Metallic Pad','Halo Pad','Sweep Pad','Rain','Soundtrack','Crystal','Atmosphere','Brightness','Goblins',
    'Echoes','Sci-fi','Sitar','Banjo','Shamisen','Koto','Kalimba','Bagpipe','Fiddle','Shanai','Tinkle Bell','Agogo',
    'Steel Drums','Woodblock','Taiko Drum','Melodic Tom','Synth Drum','Reverse Cymbal','Guitar Fret Noise','Breath Noise',
    'Seashore','Bird Tweet','Telephone Ring','Helicopter','Applause','Gunshot'];

  const DRUM_CHANNEL = 9;  // MIDI channel 10

  // The instruments ("parts") of a file: one entry per track + channel that has notes:
  // { key: "track:channel", track, channel, notes, low, high, drums, program, instrument, name }
  function listParts(midi) {
    const parts = new Map();
    midi.tracks.forEach((track, trackIndex) => {
      let trackName = '';
      let instrumentName = '';
      const programByChannel = {};  // first program change per channel
      for (const message of track) {
        if (message.type === 'track_name' && !trackName) {
          trackName = message.name.trim();
        } else if (message.type === 'instrument_name' && !instrumentName) {
          instrumentName = message.name.trim();
        } else if (message.type === 'program_change' && !(message.channel in programByChannel)) {
          programByChannel[message.channel] = message.program;
        } else if (message.type === 'note_on' && message.velocity > 0) {
          const key = trackIndex + ':' + message.channel;
          if (!parts.has(key)) {
            parts.set(key, { key, track: trackIndex, channel: message.channel, notes: 0, low: 127, high: 0 });
          }
          const part = parts.get(key);
          part.notes++;
          part.low = Math.min(part.low, message.note);
          part.high = Math.max(part.high, message.note);
        }
      }
      for (const part of parts.values()) {
        if (part.track !== trackIndex) continue;
        part.drums = part.channel === DRUM_CHANNEL;
        part.program = programByChannel[part.channel];
        part.instrument = part.drums ? 'Drums' : (part.program != null ? GM_INSTRUMENTS[part.program] : '');
        part.name = trackName || instrumentName || part.instrument || ('Track ' + (trackIndex + 1));
      }
    });
    return [...parts.values()];
  }

  // ======================================================================= conversion

  // Note after transpose and range folding. Folding moves by octaves, so the note name stays;
  // ranges narrower than an octave are not folded.
  function shapeNote(note, options) {
    note += options.transpose || 0;
    const low = options.low != null ? options.low : 0;
    const high = options.high != null ? options.high : 127;
    if (high - low >= 11) {
      while (note > high) note -= 12;
      while (note < low) note += 12;
    }
    return Math.max(0, Math.min(127, note));
  }

  // All notes as [timeMs, note, durationMs, channel], sorted by (timeMs, note).
  // A note_on pairs with the next note_off (or note_on with velocity 0) of the same channel and
  // note, as in the Python script.
  function collectNotes(midi, options) {
    const merged = mergeTracks(midi.tracks);
    let tempoMap = buildTempoMap(merged);
    if (options.speed && options.speed !== 1) {
      tempoMap = tempoMap.map(([tick, tempoUs]) => [tick, tempoUs / options.speed]);
    }
    const toMs = tick => tickToMs(tick, midi.ticksPerBeat, tempoMap);
    const keepParts = options.keep;
    const notes = [];
    const sounding = new Map();  // "channel,note" -> { start tick, track }

    const endNote = message => {
      const key = message.channel + ',' + message.note;
      if (!sounding.has(key)) return;
      const { start, track } = sounding.get(key);
      sounding.delete(key);
      if (keepParts && !keepParts.has(track + ':' + message.channel)) return;
      const startMs = toMs(start);
      let durationMs = toMs(message.abs) - toMs(start);
      if (durationMs < MIN_NOTE_MS) durationMs = MIN_NOTE_MS;
      notes.push([startMs, shapeNote(message.note, options), durationMs, message.channel]);
    };

    for (const message of merged) {
      if (message.type === 'note_on') {
        if (message.velocity === 0) endNote(message);
        else sounding.set(message.channel + ',' + message.note, { start: message.abs, track: message.track });
      } else if (message.type === 'note_off') {
        endNote(message);
      }
    }
    notes.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    return notes;
  }

  // Gives each note a motor: the preferred one (note % 5, or channel % 5 with byChannel) or the
  // next free one; when all 5 are busy, the motor whose note ends soonest is taken over.
  //
  // The Python script checks every earlier note for each new one (O(n^2): 1.4 s for 8746 notes in
  // a browser). Notes arrive sorted by time, so a note that has ended can never be busy again:
  // keeping only the still-sounding notes, in their original order, gives exactly the same result,
  // much faster. Fills stats.stolen (notes that cut another short) and stats.maxAtOnce.
  function assignMotorsAvoidingBusy(notes, byChannel, stats) {
    const events = [];  // [timeMs, motor, note, durationMs]
    let sounding = [];  // earlier events still sounding, in event order
    stats.stolen = 0;
    stats.maxAtOnce = 0;
    for (const [timeMs, note, durationMs, channel] of notes) {
      const preferred = byChannel ? channel % NUM_MOTORS : note % NUM_MOTORS;
      sounding = sounding.filter(([start, , , duration]) => timeMs < start + duration);
      const busyUntil = new Map();  // motor -> end time; insertion order as Python's dict
      for (const [start, motor, , duration] of sounding) {
        if (start <= timeMs && timeMs < start + duration) busyUntil.set(motor, start + duration);
      }
      let chosen = -1;
      for (let k = 0; k < NUM_MOTORS; k++) {
        const motor = (preferred + k) % NUM_MOTORS;
        if (!busyUntil.has(motor)) {
          chosen = motor;
          break;
        }
      }
      if (chosen < 0) {  // all busy: take the motor whose note ends soonest (the first one on a tie)
        let soonestEnd = Infinity;
        for (const [motor, end] of busyUntil) {
          if (end < soonestEnd) {
            soonestEnd = end;
            chosen = motor;
          }
        }
        stats.stolen++;
      }
      stats.maxAtOnce = Math.max(stats.maxAtOnce, sounding.length + 1);
      const event = [timeMs, chosen, note, durationMs];
      events.push(event);
      sounding.push(event);
    }
    return events;
  }

  // options (all optional; the defaults give exactly midi_to_stepper.py's output):
  //   mode:      'note' (spread, as Python) | 'channel' (one motor per instrument) | 'pitch' (low -> motor 0)
  //   speed:     playback speed factor (1 = original)
  //   keep:      Set of part keys ("track:channel", see listParts) to convert; all when absent
  //   transpose: semitones
  //   low, high: fold notes into this MIDI note range
  // Returns { events: [[timeMs, motor, note, durationMs]], stats: { stolen, maxAtOnce } }.
  function convert(midi, options) {
    options = Object.assign({ mode: 'note', speed: 1, keep: null, transpose: 0, low: null, high: null }, options || {});
    const notes = collectNotes(midi, options);
    const stats = { stolen: 0, maxAtOnce: 0 };
    let events;
    if (options.mode === 'pitch') {
      events = notes.map(([time, note, duration]) => [time, Math.min(NUM_MOTORS - 1, Math.floor(note / NOTES_PER_MOTOR)), note, duration]);
    } else {
      events = assignMotorsAvoidingBusy(notes, options.mode === 'channel', stats);
    }
    return { events, stats };
  }

  // Events -> the bytes of a .stepper file (format at the top of this file).
  function toStepperBytes(events) {
    const buffer = new ArrayBuffer(12 + 8 * events.length);
    const view = new DataView(buffer);
    [0x53, 0x54, 0x50, 0x4d].forEach((char, i) => view.setUint8(i, char));  // "STPM"
    view.setUint8(4, 1);  // version
    view.setUint32(8, events.length, true);
    events.forEach(([timeMs, motor, note, durationMs], i) => {
      const offset = 12 + 8 * i;
      view.setUint32(offset, timeMs, true);
      view.setUint8(offset + 4, motor);
      view.setUint8(offset + 5, note);
      view.setUint16(offset + 6, Math.min(durationMs, 0xffff), true);
    });
    return new Uint8Array(buffer);
  }

  // Note count, length and notes per motor of a convert() result (as summarize_events() in the
  // Python script).
  function summarize(conversion) {
    const events = conversion.events;
    const perMotor = new Array(NUM_MOTORS).fill(0);
    let start = Infinity, end = 0;
    for (const [timeMs, motor, , durationMs] of events) {
      if (motor >= 0 && motor < NUM_MOTORS) perMotor[motor]++;
      start = Math.min(start, timeMs);
      end = Math.max(end, timeMs + durationMs);
    }
    return {
      notes: events.length,
      lengthMs: events.length ? Math.max(0, end - start) : 0,
      perMotor,
    };
  }

  global.StepperConverter = { parseMidi, listParts, convert, toStepperBytes, summarize };
})(typeof window !== 'undefined' ? window : globalThis);
