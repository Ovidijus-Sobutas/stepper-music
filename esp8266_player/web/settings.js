// Settings of the web page: timings, list sizes and the choices offered in the drop-down lists.
// Every other page file reads its values from here, so this is the one place to change them.
// After a change, run tools\prepare_firmware.ps1 and flash the ESP8266 (the page is built
// into the firmware).
//
// This is a .js file and not .json so that every value can carry a comment. It is loaded
// before the other scripts (see index.html).
//
// Limits the firmware enforces are noted; values outside them are refused by the player.

const SETTINGS = Object.freeze({
  // ---------------------------------------------------------------- talking to the player
  // The player's Wi-Fi can be weak, so every request has a timeout and lists are retried.
  network: {
    requestTimeoutMs: 4000,   // buttons and short answers
    listTimeoutMs: 6000,      // longer JSON answers: songs, playlists, sound, info, Wi-Fi scan
    statusTimeoutMs: 3000,    // the once-a-second status poll
    uploadTimeoutMs: 60000,   // one song upload (a 70 KB song takes a few seconds)
    retryDelayMs: 3000,       // retry a failed list load after this long
  },

  // ---------------------------------------------------------------- now playing
  status: {
    refreshEveryMs: 1000,                 // how often the page asks what is playing
    refreshAfterButtonMs: [250, 600, 1100, 1800],  // extra quick checks right after a button press
  },
  toastMs: 2500,                          // how long a message stays at the bottom of the page

  // ---------------------------------------------------------------- songs
  songs: {
    perPage: 8,               // songs shown before "Show more"
    reloadAfterDemosMs: 2000, // the player writes ~120 KB after "Restore demo songs"; reload then
  },

  // ---------------------------------------------------------------- Wi-Fi settings
  wifi: {
    scanPollMs: 1500,         // the scan runs on the player; ask again this often until it's done
    reloadAfterRemoveMs: 1500,
    reloadAfterSaveMs: 8000,  // connecting to a newly saved network takes a few seconds
  },

  // ---------------------------------------------------------------- drop-down choices
  // { value, text } pairs; "selected: true" marks the starting choice where it matters.
  choices: {
    repeat: [                 // queue repeat mode (firmware: off | all | one)
      { value: 'off', text: 'Off' },
      { value: 'all', text: 'All' },
      { value: 'one', text: 'One song' },
    ],
    restBetweenSongs: [       // seconds of silence between songs (firmware: 0-60)
      { value: '0', text: 'None' },
      { value: '2', text: '2 s' },
      { value: '5', text: '5 s' },
      { value: '10', text: '10 s (motor cooldown)' },
    ],

    // Sound style "Fine-tune" (firmware checks each value; see Sound.cpp)
    octave: [
      { value: '-1', text: '−1 (slower)' },
      { value: '0', text: '0' },
      { value: '1', text: '+1 (faster)' },
    ],
    microstepsTogether: [     // the style's "grain": 1, 2 or 4 microsteps per interrupt
      { value: '1', text: '1 (smoothest)' },
      { value: '2', text: '2' },
      { value: '4', text: '4' },
    ],
    motorOffWhenSilent: [     // milliseconds before a silent motor is switched off
      { value: '200', text: 'after 0.2 s' },
      { value: '400', text: 'after 0.4 s' },
      { value: '1000', text: 'after 1 s' },
      { value: '5000', text: 'after 5 s' },
      { value: '60000', text: 'stay on' },
    ],
    driverMicrostepping: [    // must match the STEP_SIZE jumpers on the GT2560
      { value: '16', text: '1/16' },
      { value: '8', text: '1/8' },
      { value: '4', text: '1/4' },
      { value: '2', text: '1/2' },
      { value: '1', text: 'full step' },
    ],

    // Convert MIDI card
    motorAssignment: [
      { value: 'note', text: 'Spread (like the Python converter)' },
      { value: 'channel', text: 'One motor per instrument' },
      { value: 'pitch', text: 'By pitch (low notes → motor 0)' },
    ],
    noteRange: [              // "lowest,highest" MIDI note; notes outside are folded in by octaves
      { value: '-1,-1', text: 'Off (as written)' },
      { value: '36,84', text: 'C2–C6' },
      { value: '40,88', text: 'E2–E6' },
      { value: '48,84', text: 'C3–C6' },
      { value: '48,96', text: 'C3–C7' },
    ],
    transpose: [              // semitones
      { value: '-12', text: '−12 (octave down)' },
      { value: '-7', text: '−7' },
      { value: '-5', text: '−5' },
      { value: '-3', text: '−3' },
      { value: '-2', text: '−2' },
      { value: '-1', text: '−1' },
      { value: '0', text: '0', selected: true },
      { value: '1', text: '+1' },
      { value: '2', text: '+2' },
      { value: '3', text: '+3' },
      { value: '5', text: '+5' },
      { value: '7', text: '+7' },
      { value: '12', text: '+12 (octave up)' },
    ],
  },

  // ---------------------------------------------------------------- Convert MIDI card
  convert: {
    motorColors: ['#2563eb', '#16a34a', '#d97706', '#db2777', '#7c3aed'],  // piano roll, motor 0-4
    listenVolume: 0.08,          // "Listen" volume (square waves are loud)
    listenScheduleAheadS: 3,     // "Listen": notes are queued in the browser this far ahead...
    listenScheduleEveryMs: 500,  // ...and topped up this often
  },
});
