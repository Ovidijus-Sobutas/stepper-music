// Music Player page logic: now playing + queue, songs, playlists, sound style, motor test, settings.
// Talks to the ESP8266's HTTP API (see esp8266_player/src/web/WebPortal.cpp). The Convert MIDI
// card is in convert.js, the MIDI -> .stepper conversion in converter.js.
'use strict';

// =====================================================================================
// Shared helpers
// =====================================================================================

const byId = id => document.getElementById(id);

// The scripts are loaded in order by the loader at the end of index.html (with retries), so
// SETTINGS (settings.js), StepperConverter (converter.js) and openConverter (convert.js) exist.

// Timings, page sizes and drop-down choices live in settings.js (SETTINGS).
// The player's Wi-Fi can be weak: every request has a timeout, so a lost answer never
// leaves the page waiting forever.
const LIST_TIMEOUT_MS = SETTINGS.network.listTimeoutMs;
const RETRY_DELAY_MS = SETTINGS.network.retryDelayMs;

function request(url, options, timeoutMs) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs || SETTINGS.network.requestTimeoutMs);
  const fetchOptions = Object.assign({ signal: controller.signal, cache: 'no-store' }, options || {});
  return fetch(url, fetchOptions).finally(() => clearTimeout(timer));
}

function getJson(url, timeoutMs) {
  return request(url, null, timeoutMs).then(response => response.json());
}

// POST /api/<path>?<params>. An optional note is shown in the status line at once.
// The player answers an empty body on success, or an error text; any text is shown.
function callApi(path, params, note, timeoutMs) {
  if (note) byId('playerState').textContent = note;
  const query = params ? '?' + new URLSearchParams(params) : '';
  return request('/api/' + path + query, { method: 'POST' }, timeoutMs)
    .then(response => response.text().then(text => {
      if (!response.ok || text) showToast(text);
      refreshStatusSoon();
    }))
    .catch(() => {
      showToast('No answer from the player, trying again…');
      refreshStatusSoon();
    });
}

function showToast(text) {
  const toast = byId('toast');
  toast.textContent = text;
  toast.style.opacity = 1;
  clearTimeout(toast.hideTimer);
  toast.hideTimer = setTimeout(() => { toast.style.opacity = 0; }, SETTINGS.toastMs);
}

// 83500 -> "1:23"
function formatTime(ms) {
  const seconds = Math.floor(ms / 1000);
  return Math.floor(seconds / 60) + ':' + String(seconds % 60).padStart(2, '0');
}

// 90061 -> "1 d 1 h 1 min"
function formatDuration(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor(seconds % 86400 / 3600);
  const minutes = Math.floor(seconds % 3600 / 60);
  return (days ? days + ' d ' : '') + (days || hours ? hours + ' h ' : '') + minutes + ' min';
}

// Bytes -> "12 KB" / "1.5 MB"
function formatSize(bytes) {
  return bytes < 1048576 ? Math.round(bytes / 1024) + ' KB' : (bytes / 1048576).toFixed(1) + ' MB';
}

function makeButton(text, className, onClick) {
  const button = document.createElement('button');
  button.textContent = text;
  if (className) button.className = className;
  button.onclick = onClick;
  return button;
}

function makeElement(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text != null) element.textContent = text;
  return element;
}

// A list row: name (+ optional muted detail line) on the left, buttons on the right.
function makeListItem(title, detail, buttons) {
  const item = makeElement('div', 'list-item');
  const text = makeElement('div');
  text.append(makeElement('b', '', title));
  if (detail != null) text.append(makeElement('div', 'muted', detail));
  const actions = makeElement('div', 'item-actions');
  actions.append(...buttons);
  item.append(text, actions);
  return item;
}

// Shows a muted message in an empty list box, or clears it for items.
function resetListBox(box, emptyMessage) {
  box.textContent = '';
  box.className = emptyMessage ? 'muted' : '';
  if (emptyMessage) box.textContent = emptyMessage;
}

function fillSelect(select, placeholder, options) {
  select.textContent = '';
  const first = makeElement('option', '', placeholder);
  first.value = '';
  select.appendChild(first);
  for (const { value, text } of options) {
    const option = makeElement('option', '', text);
    option.value = value;
    select.appendChild(option);
  }
}

// =====================================================================================
// Now playing, transport buttons, play queue (polls /status every second)
// =====================================================================================

let playerStatus = {};          // last /status answer
let statusRequestBusy = false;  // one /status request at a time on a slow link
let lastWifiState = null;       // to open the Wi-Fi section only when the connection is lost

// After a button press the player needs a moment: check the result quickly a few times
// instead of waiting for the next 1 s poll.
function refreshStatusSoon() {
  SETTINGS.status.refreshAfterButtonMs.forEach(delay => setTimeout(refreshStatus, delay));
}

function refreshStatus() {
  if (statusRequestBusy) return;
  statusRequestBusy = true;
  getJson('/status', SETTINGS.network.statusTimeoutMs)
    .then(status => {
      playerStatus = status;
      showNowPlaying(status);
      showQueue(status.queue || {});
      showConnection(status);
    })
    .catch(() => { byId('playerState').textContent = 'No connection to the player'; })
    .finally(() => { statusRequestBusy = false; });
}

function showNowPlaying(status) {
  const playing = status.mode === 'song';
  const testing = status.mode === 'test';
  let state = status.msg || 'Ready';
  if (playing) state = status.paused ? 'Paused' : 'Playing';
  else if (testing) state = 'Motor test';
  byId('playerState').textContent = state;
  byId('songTitle').textContent = playing ? status.song : testing ? 'Motor ' + status.motor : '';
  const percent = playing && status.len ? Math.min(100, 100 * status.t / status.len) : 0;
  byId('songProgress').style.width = percent + '%';
  byId('timeElapsed').textContent = formatTime(playing ? status.t : 0);
  byId('timeTotal').textContent = formatTime(playing ? status.len : 0);
  byId('pauseButton').disabled = !playing;
  byId('pauseButton').textContent = status.paused ? 'Resume' : 'Pause';
}

function showQueue(queue) {
  byId('prevButton').disabled = !queue.active;
  byId('nextButton').disabled = !queue.active;
  const waitingForNext = queue.active && queue.wait > 0;  // pause between songs
  if (waitingForNext) {
    byId('playerState').textContent = 'Next song in ' + Math.ceil(queue.wait / 1000) + ' s';
    byId('songTitle').textContent = queue.next;
  }
  let info = '';
  if (queue.active) {
    if (queue.size > 1) info += 'Song ' + queue.pos + ' of ' + queue.size + ' · ';
    info += queue.source;
    if (queue.shuffle) info += ' · shuffled';
    if (queue.next && !waitingForNext) info += ' · next: ' + queue.next;
  }
  byId('queueInfo').textContent = info;
  // Do not overwrite a dropdown the user is choosing in right now.
  if (document.activeElement !== byId('repeatMode')) {
    byId('repeatMode').value = queue.repeat || 'off';
  }
  if (document.activeElement !== byId('restBetweenSongs')) {
    byId('restBetweenSongs').value = String(queue.rest != null ? queue.rest : 2);
  }
}

function showConnection(status) {
  const connected = status.wifi === 'CONNECTED';
  const wifi = byId('wifiStatus');
  wifi.textContent = status.wifi + (status.ssid ? ' · ' + status.ssid : '') + (status.rssi ? ' · ' + status.rssi + ' dBm' : '');
  wifi.className = connected ? 'ok' : 'bad';
  byId('networkAddress').textContent = connected
    ? status.ip + ' · musicplayer.local'
    : (status.ap ? 'hotspot ' + status.ap : '-');
  const gt = byId('gt2560Status');
  gt.textContent = status.gt ? 'connected · v' + status.gtver : 'NOT CONNECTED';
  gt.className = status.gt ? 'ok' : 'bad';
  // Not connected: open the Wi-Fi settings, but only on first load or when the connection was
  // just lost, so the user can still close them.
  if (!connected && (lastWifiState === null || lastWifiState === 'CONNECTED')) {
    byId('wifiSection').open = true;
  }
  lastWifiState = status.wifi;
}

function togglePause() {
  if (playerStatus.paused) callApi('resume', null, 'Resuming…');
  else callApi('pause', null, 'Pausing…');
}

function testMotor(motor) {
  callApi('test', { motor }, motor === 'all' ? 'Testing all motors…' : 'Testing motor ' + motor + '…');
}

function setUpPlayerControls() {
  byId('prevButton').onclick = () => callApi('prev', null, 'Previous…');
  byId('pauseButton').onclick = togglePause;
  byId('stopButton').onclick = () => callApi('stop', null, 'Stopping…');
  byId('nextButton').onclick = () => callApi('next', null, 'Next…');
  byId('playAllButton').onclick = () => callApi('queue', { source: 'all' }, 'Playing all songs…');
  byId('shuffleAllButton').onclick = () => callApi('queue', { source: 'all', shuffle: 1 }, 'Shuffling all songs…');
  byId('repeatMode').onchange = event => callApi('queue-settings', { repeat: event.target.value });
  byId('restBetweenSongs').onchange = event => callApi('queue-settings', { rest: event.target.value });
  document.querySelectorAll('[data-motor]').forEach(button => {
    button.onclick = () => testMotor(button.dataset.motor);
  });
}

// =====================================================================================
// Songs: list with search and "Show more", play / rename / delete, upload
// =====================================================================================

const SONGS_PER_PAGE = SETTINGS.songs.perPage;
let allSongs = [];                  // [{ name, len, notes, bytes }] from /api/songs
let songsShown = SONGS_PER_PAGE;    // grows with "Show more", back to one page on a new search

function loadSongs() {
  getJson('/api/songs', LIST_TIMEOUT_MS)
    .then(answer => {
      allSongs = answer.songs;
      renderSongs();
      if (playlistBeingEdited) renderPlaylistEditor();  // "(missing)" marks and the song dropdown
      loadPlaylists();
      byId('storageUsed').textContent = formatSize(answer.used) + ' of ' + formatSize(answer.total) + ' used';
    })
    .catch(error => {
      // Say why instead of showing "Loading…" forever, then try again.
      console.error('loading the song list failed:', error);
      if (!allSongs.length) resetListBox(byId('songList'), describeLoadError(error) + ' Trying again…');
      setTimeout(loadSongs, RETRY_DELAY_MS);
    });
}

function describeLoadError(error) {
  if (error && error.name === 'AbortError') return 'The player took too long to send the song list.';
  if (error instanceof TypeError) return 'No connection to the player.';
  return 'Could not show the song list (' + (error && error.message ? error.message : error) + ').';
}

// "spooky skel" finds "Spooky_Scary_Skeletons": case, "_" and "-" are ignored, every word must match.
function simplifyForSearch(text) {
  return text.toLowerCase().replace(/[_-]+/g, ' ');
}

function matchesSearch(name, query) {
  const simpleName = simplifyForSearch(name);
  return simplifyForSearch(query).split(' ').every(word => simpleName.includes(word));
}

function renderSongs() {
  const box = byId('songList');
  const query = byId('songSearch').value.trim();
  const found = allSongs.filter(song => matchesSearch(song.name, query));
  if (!allSongs.length) resetListBox(box, 'No songs yet. Add some below.');
  else if (!found.length) resetListBox(box, 'No song matches "' + query + '".');
  else resetListBox(box);
  for (const song of found.slice(0, songsShown)) {
    const detail = formatTime(song.len) + ' · ' + song.notes + ' notes · ' + formatSize(song.bytes);
    box.appendChild(makeListItem(song.name, detail, [
      makeButton('Play', '', () => playSong(song.name)),
      makeButton('✎', 'ghost', () => renameSong(song.name)),
      makeButton('✕', 'danger', () => deleteSong(song.name)),
    ]));
  }
  const hiddenCount = found.length - songsShown;
  const moreButton = byId('showMoreSongsButton');
  moreButton.hidden = hiddenCount <= 0;
  moreButton.textContent = 'Show more (' + hiddenCount + ' more)';
}

function playSong(name) {
  byId('songTitle').textContent = name;
  callApi('play', { song: name }, 'Starting…');
}

function renameSong(name) {
  const newName = prompt('New name for "' + name + '":', name);
  if (newName && newName !== name) callApi('rename', { song: name, to: newName }).then(loadSongs);
}

function deleteSong(name) {
  if (confirm('Delete "' + name + '"?')) callApi('delete', { song: name }).then(loadSongs);
}

// The player answers /api/demos at once and writes the ~120 KB of songs right after, so the
// list is reloaded a little later (song requests wait until the writing is done).
function restoreDemoSongs(event) {
  event.preventDefault();
  if (confirm('Copy the demo songs back? Songs with the same names are replaced.')) {
    callApi('demos').then(() => setTimeout(loadSongs, SETTINGS.songs.reloadAfterDemosMs));
  }
}

// A .mid file opens the Convert MIDI card (convert.js): instruments and settings are chosen
// there and it is converted to .stepper here in the browser. A .stepper file is uploaded as it is.
// Resolves with { blob, name, info } to upload, or null when the conversion was cancelled.
async function prepareUpload(file) {
  const isMidi = /\.midi?$/i.test(file.name);
  if (!isMidi) return { blob: file, name: file.name.replace(/\.[^.]*$/, ''), info: '' };
  return openConverter(file);
}

// Uploads the chosen files one after another (the player stores one upload at a time).
// XMLHttpRequest instead of fetch because only it reports upload progress.
async function uploadFiles(files, index) {
  if (index >= files.length) {
    byId('uploadBar').hidden = true;
    byId('songFileInput').value = '';
    loadSongs();
    return;
  }
  const file = files[index];
  const next = () => uploadFiles(files, index + 1);
  let upload;
  try {
    upload = await prepareUpload(file);
  } catch (error) {
    showToast('"' + file.name + '": ' + error.message);
    next();
    return;
  }
  if (!upload) {  // conversion cancelled
    next();
    return;
  }
  const form = new FormData();
  form.append('file', upload.blob, upload.name + '.stepper');
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/upload?name=' + encodeURIComponent(upload.name));
  xhr.timeout = SETTINGS.network.uploadTimeoutMs;
  byId('uploadBar').hidden = false;
  byId('uploadProgress').style.width = '0';
  xhr.upload.onprogress = event => {
    if (event.lengthComputable) byId('uploadProgress').style.width = (100 * event.loaded / event.total) + '%';
  };
  xhr.onload = () => {
    if (xhr.status === 200) showToast('Added "' + upload.name + '"' + upload.info);
    else showToast('"' + upload.name + '": ' + xhr.responseText);
    next();
  };
  xhr.onerror = xhr.ontimeout = () => {
    showToast('"' + upload.name + '": upload failed');
    next();
  };
  showToast('Uploading "' + upload.name + '"' + upload.info + '… (' + (index + 1) + ' of ' + files.length + ')');
  xhr.send(form);
}

function setUpSongs() {
  byId('songSearch').oninput = () => {  // filters live on every keystroke
    songsShown = SONGS_PER_PAGE;
    renderSongs();
  };
  byId('showMoreSongsButton').onclick = () => {
    songsShown += SONGS_PER_PAGE;
    renderSongs();
  };
  byId('restoreDemosLink').onclick = restoreDemoSongs;
  byId('songFileInput').onchange = event => {
    if (event.target.files.length) uploadFiles(event.target.files, 0);
  };
}

// =====================================================================================
// Playlists: list, play, shuffle, delete, and an editor for an ordered song list
// =====================================================================================

let playlistBeingEdited = null;  // { originalName ('' for a new one), songs: [names] } or null

function loadPlaylists() {
  getJson('/api/playlists', LIST_TIMEOUT_MS)
    .then(answer => renderPlaylists(answer.playlists))
    .catch(() => setTimeout(loadPlaylists, RETRY_DELAY_MS));
}

function renderPlaylists(playlists) {
  const box = byId('playlistList');
  resetListBox(box, playlists.length ? null : 'No playlists yet.');
  for (const playlist of playlists) {
    const count = playlist.songs.length + ' song' + (playlist.songs.length === 1 ? '' : 's');
    const shuffleButton = makeButton('🔀', 'ghost',
      () => callApi('queue', { source: 'playlist', name: playlist.name, shuffle: 1 }, 'Shuffling…'));
    shuffleButton.title = 'Shuffle';
    box.appendChild(makeListItem(playlist.name, count, [
      makeButton('Play', '', () => callApi('queue', { source: 'playlist', name: playlist.name }, 'Starting…')),
      shuffleButton,
      makeButton('✎', 'ghost', () => openPlaylistEditor(playlist)),
      makeButton('✕', 'danger', () => deletePlaylist(playlist.name)),
    ]));
  }
}

function deletePlaylist(name) {
  if (confirm('Delete playlist "' + name + '"? The songs stay on the player.')) {
    callApi('playlist-delete', { name }).then(loadPlaylists);
  }
}

// playlist = an entry of /api/playlists to edit, or null for a new one.
function openPlaylistEditor(playlist) {
  playlistBeingEdited = {
    originalName: playlist ? playlist.name : '',
    songs: playlist ? playlist.songs.slice() : [],
  };
  byId('playlistName').value = playlistBeingEdited.originalName;
  byId('playlistEditor').hidden = false;
  renderPlaylistEditor();
  byId('playlistName').focus();
}

function closePlaylistEditor() {
  playlistBeingEdited = null;
  byId('playlistEditor').hidden = true;
}

function renderPlaylistEditor() {
  const songs = playlistBeingEdited.songs;
  const songNames = allSongs.map(song => song.name);
  const box = byId('playlistSongs');
  resetListBox(box, songs.length ? null : 'No songs yet: add some below.');
  songs.forEach((name, i) => {
    const item = makeElement('div', 'list-item');
    const label = makeElement('div', '', (i + 1) + '. ' + name);
    if (!songNames.includes(name)) {  // deleted or renamed since the playlist was saved
      label.className = 'muted';
      label.textContent += ' (missing)';
    }
    const moveBy = offset => {
      [songs[i], songs[i + offset]] = [songs[i + offset], songs[i]];
      renderPlaylistEditor();
    };
    const upButton = makeButton('↑', 'ghost', () => moveBy(-1));
    const downButton = makeButton('↓', 'ghost', () => moveBy(1));
    upButton.disabled = i === 0;
    downButton.disabled = i === songs.length - 1;
    const removeButton = makeButton('✕', 'danger', () => {
      songs.splice(i, 1);
      renderPlaylistEditor();
    });
    const actions = makeElement('div', 'item-actions');
    actions.append(upButton, downButton, removeButton);
    item.append(label, actions);
    box.appendChild(item);
  });
  fillSelect(byId('playlistAddSong'), 'Choose a song…', songNames.map(name => ({ value: name, text: name })));
}

// Saves under the new name; a renamed playlist is saved first, then the old name deleted.
async function savePlaylist() {
  const name = byId('playlistName').value.trim();
  const { originalName, songs } = playlistBeingEdited;
  try {
    const body = new URLSearchParams({ name, songs: songs.join('\n') });
    const response = await request('/api/playlist', { method: 'POST', body }, LIST_TIMEOUT_MS);
    const text = await response.text();
    if (!response.ok) {
      showToast(text);
      return;
    }
    if (originalName && originalName !== name) {
      await request('/api/playlist-delete?name=' + encodeURIComponent(originalName), { method: 'POST' });
    }
    showToast('Saved "' + name + '"');
    closePlaylistEditor();
    loadPlaylists();
  } catch (error) {
    showToast('No answer from the player');
  }
}

function setUpPlaylists() {
  byId('newPlaylistButton').onclick = () => openPlaylistEditor(null);
  byId('playlistAddSong').onchange = event => {
    if (!event.target.value) return;
    playlistBeingEdited.songs.push(event.target.value);
    renderPlaylistEditor();
  };
  byId('playlistAddAllButton').onclick = () => {
    for (const song of allSongs) {
      if (!playlistBeingEdited.songs.includes(song.name)) playlistBeingEdited.songs.push(song.name);
    }
    renderPlaylistEditor();
  };
  byId('playlistCancelButton').onclick = closePlaylistEditor;
  byId('playlistSaveButton').onclick = savePlaylist;
}

// =====================================================================================
// Sound style: preset buttons + fine-tune dropdowns, all from /api/sound
// =====================================================================================

let soundSettings = null;  // last /api/sound answer: { style, styles: [{id, name, desc}], s: {key: value} }

function loadSound() {
  getJson('/api/sound', LIST_TIMEOUT_MS)
    .then(answer => {
      soundSettings = answer;
      renderSoundStyles(answer);
      document.querySelectorAll('[data-sound-key]').forEach(select => {
        select.value = String(answer.s[select.dataset.soundKey]);
      });
    })
    .catch(() => setTimeout(loadSound, RETRY_DELAY_MS));
}

function renderSoundStyles(answer) {
  const box = byId('soundStyles');
  box.textContent = '';
  for (const style of answer.styles) {
    const button = makeButton(style.name, style.id === answer.style ? 'selected' : '', () => chooseStyle(style));
    button.title = style.desc;
    box.appendChild(button);
  }
  // Fine-tuned settings that match no preset show as an extra "Custom" style.
  if (answer.style === 'custom') {
    box.appendChild(makeButton('Custom', 'selected', () => { byId('fineTune').open = true; }));
  }
  const current = answer.styles.find(style => style.id === answer.style);
  byId('soundStyleDescription').textContent = current ? current.desc : 'Your own settings (see Fine-tune).';
}

function chooseStyle(style) {
  markSelectedStyle(style.id);  // at once, before the player answers
  request('/api/sound?style=' + style.id, { method: 'POST' })
    .then(() => {
      showToast('Style: ' + style.name + ' (from the next note)');
      loadSound();
    })
    .catch(() => showToast('No answer from the player'));
}

function markSelectedStyle(id) {
  document.querySelectorAll('#soundStyles button').forEach(button => {
    const isChosen = soundSettings && soundSettings.styles.some(style => style.id === id && style.name === button.textContent);
    button.classList.toggle('selected', isChosen);
  });
}

function setUpSound() {
  document.querySelectorAll('[data-sound-key]').forEach(select => {
    select.onchange = () => {
      request('/api/sound?key=' + select.dataset.soundKey + '&value=' + select.value, { method: 'POST' })
        .then(response => response.text().then(text => {
          if (!response.ok) showToast(text);
          loadSound();
        }))
        .catch(() => showToast('No answer from the player'));
    };
  });
}

// =====================================================================================
// Settings: Wi-Fi networks, about this player, restart, factory reset
// =====================================================================================

// The player scans in the background: it answers { scanning: true } until the list is ready.
function scanNetworks() {
  getJson('/scan', LIST_TIMEOUT_MS)
    .then(answer => {
      if (answer.scanning) {
        setTimeout(scanNetworks, SETTINGS.wifi.scanPollMs);
        return;
      }
      fillSelect(byId('scannedNetworks'),
        answer.networks.length ? 'Choose a network…' : 'No networks found',
        answer.networks.map(network => ({
          value: network.ssid,
          text: network.ssid + ' (' + network.rssi + ' dBm' + (network.open ? ', open' : '') + ')',
        })));
    })
    .catch(() => setTimeout(scanNetworks, RETRY_DELAY_MS));
}

// /api/info fills both the saved networks list and the "About this player" table.
function loadPlayerInfo() {
  getJson('/api/info', LIST_TIMEOUT_MS)
    .then(info => {
      renderSavedNetworks(info.networks);
      renderAbout(info);
    })
    .catch(() => setTimeout(loadPlayerInfo, RETRY_DELAY_MS));
}

function renderSavedNetworks(networks) {
  const box = byId('savedNetworks');
  resetListBox(box, networks.length ? null : 'No saved networks.');
  for (const network of networks) {
    const item = makeElement('div', 'list-item');
    const name = makeElement('div', network.current ? 'ok' : '', network.ssid + (network.current ? ' · connected' : ''));
    item.append(name, makeButton('✕', 'danger', () => removeNetwork(network)));
    box.appendChild(item);
  }
}

function removeNetwork(network) {
  const warning = network.current ? ' The player is using it now and switches to another saved network.' : '';
  if (confirm('Remove "' + network.ssid + '"?' + warning)) {
    callApi('wifi-remove', { ssid: network.ssid }).then(() => setTimeout(loadPlayerInfo, SETTINGS.wifi.reloadAfterRemoveMs));
  }
}

function renderAbout(info) {
  const rows = [
    ['Player firmware', info.fw],
    ['GT2560 firmware', info.gt ? info.gtver : 'not connected'],
    ['Running for', formatDuration(info.up)],
    ['Free memory', formatSize(info.heap) + ' (largest block ' + formatSize(info.block) + ')'],
    ['Storage', formatSize(info.used) + ' of ' + formatSize(info.total)],
    ['Library', info.songs + ' songs · ' + info.playlists + ' playlists'],
    ['Setup hotspot', info.ap],
    ['Name on the network', info.host],
    ['Last start', info.reset],
    ['ESP8266 core', info.core],
  ];
  const table = byId('playerInfo');
  resetListBox(table);
  for (const [label, value] of rows) {
    const row = makeElement('div', 'row');
    row.append(makeElement('span', '', label), makeElement('span', '', value));
    table.appendChild(row);
  }
}

// The player switches networks right after answering, so the answer can get lost: that is
// reported as probably saved rather than as an error.
function saveNetwork(event) {
  event.preventDefault();
  showToast('Saving…');
  const body = new URLSearchParams({ ssid: byId('networkSsid').value, pass: byId('networkPassword').value });
  request('/save', { method: 'POST', body }, LIST_TIMEOUT_MS)
    .then(response => response.text())
    .then(showToast)
    .then(() => setTimeout(loadPlayerInfo, SETTINGS.wifi.reloadAfterSaveMs))
    .catch(() => showToast('Saved. If this page stops updating, read the new address on the display.'));
}

function forgetAllNetworks() {
  if (!confirm('Forget ALL saved Wi-Fi networks and open the setup hotspot?')) return;
  request('/forget', { method: 'POST' })
    .then(response => response.text())
    .then(showToast)
    .catch(() => showToast('No answer from the player'));
}

function factoryReset() {
  const typed = prompt('This erases ALL songs, playlists, Wi-Fi networks and settings.\nType RESET to confirm:');
  if (typed === 'RESET') callApi('factory-reset', { confirm: 'RESET' });
  else if (typed !== null) showToast('Not confirmed: nothing was erased');
}

function setUpSettings() {
  byId('wifiSection').ontoggle = () => {
    if (byId('wifiSection').open) {
      scanNetworks();
      loadPlayerInfo();
    }
  };
  byId('aboutSection').ontoggle = () => {
    if (byId('aboutSection').open) loadPlayerInfo();
  };
  byId('scannedNetworks').onchange = event => {
    if (event.target.value) byId('networkSsid').value = event.target.value;
  };
  byId('addNetworkForm').onsubmit = saveNetwork;
  byId('forgetNetworksButton').onclick = forgetAllNetworks;
  byId('restartButton').onclick = () => {
    if (confirm('Restart the player? The music stops.')) callApi('restart');
  };
  byId('factoryResetButton').onclick = factoryReset;
}

// =====================================================================================
// Start
// =====================================================================================

// Drop-down lists marked data-choices="<name>" get their options from SETTINGS.choices.<name>.
function fillChoiceLists() {
  document.querySelectorAll('select[data-choices]').forEach(select => {
    const choices = SETTINGS.choices[select.dataset.choices];
    if (!choices) throw new Error('settings.js has no choices.' + select.dataset.choices);
    choices.forEach(choice => {
      const option = document.createElement('option');
      option.value = choice.value;
      option.textContent = choice.text;
      option.selected = Boolean(choice.selected);
      select.appendChild(option);
    });
  });
}

fillChoiceLists();
setUpPlayerControls();
setUpSongs();
setUpPlaylists();
setUpSound();
setUpSettings();

refreshStatus();
loadSongs();      // loads the playlists too once the songs are in
loadSound();
setInterval(refreshStatus, SETTINGS.status.refreshEveryMs);
