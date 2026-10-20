(() => {
'use strict';

const ICONS = {
  plus:    '<path d="M12 5v14M5 12h14"/>',
  folder:  '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  trash:   '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4h6v3"/>',
  search:  '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
  sliders: '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
  arrow:   '<path d="M7 17 17 7M8 7h9v9"/>',
  check:   '<path d="m5 12 5 5 9-10"/>',
  x:       '<path d="M6 6l12 12M18 6 6 18"/>',
  alert:   '<path d="M12 4 2.5 20h19z"/><path d="M12 10v5M12 18h.01"/>',
  import:  '<path d="M12 4v11M7 10l5 5 5-5M5 20h14"/>'
};
const icon = n => `<svg viewBox="0 0 24 24">${ICONS[n]}</svg>`;
const $ = s => document.querySelector(s);
const $$ = s => [...document.querySelectorAll(s)];
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

/* ---------- bridge to the C++ side ---------- */
const hasHost = !!(window.chrome && window.chrome.webview);
const pending = new Map();
let seq = 0;
if (hasHost) {
  window.chrome.webview.addEventListener('message', e => {
    const m = e.data, res = pending.get(m.id);
    if (res) { pending.delete(m.id); res(m); }
  });
}
function call(cmd, ...args) {
  if (!hasHost) return mockCall(cmd, args);
  return new Promise(res => {
    const id = ++seq;
    pending.set(id, res);
    window.chrome.webview.postMessage([id, cmd, ...args].join('\x1f'));
  });
}

/* In a normal browser (index.html opened directly) a tiny fake backend lets you preview the UI. */
const mock = {
  projects: [
    { name: 'Odium Launcher', path: 'C:\\Users\\you\\AndroidStudioProjects\\OdiumLauncher', pkg: 'com.odium.launcher', created: Date.now()/1000 - 86400*9, opened: Date.now()/1000 - 3600*2, missing: false },
    { name: 'OdiumWeather', path: 'C:\\Users\\you\\AndroidStudioProjects\\OdiumWeather', pkg: 'com.odium.weather', created: Date.now()/1000 - 86400*20, opened: Date.now()/1000 - 86400*3, missing: false },
    { name: 'Terminal', path: 'C:\\Users\\you\\AndroidStudioProjects\\Terminal', pkg: 'com.odium.terminal', created: Date.now()/1000 - 86400*40, opened: 0, missing: false },
    { name: 'Old Experiment', path: 'D:\\gone\\OldExperiment', pkg: 'com.example.oldexperiment', created: Date.now()/1000 - 86400*90, opened: Date.now()/1000 - 86400*60, missing: true }
  ],
  settings: { studioPath: 'C:\\Program Files\\Android\\Android Studio\\bin\\studio64.exe', studioFound: true, location: 'C:\\Users\\you\\AndroidStudioProjects', prefix: 'com.example', accent: '#ff2d4d' },
  dataPath: 'C:\\Users\\you\\AppData\\Roaming\\OdiumProjects'
};
function mockCall(cmd, a) {
  return new Promise(res => setTimeout(() => {
    const r = { ok: true, state: JSON.parse(JSON.stringify(mock)) };
    if (cmd === 'open') r.toast = { text: 'Opening ' + (mock.projects.find(p => p.path === a[0]) || {}).name + ' in Android Studio…', error: false };
    if (cmd === 'remove') { mock.projects = mock.projects.filter(p => p.path !== a[0]); r.state = JSON.parse(JSON.stringify(mock)); r.toast = { text: 'Removed from the list. Files were left untouched.', error: false }; }
    if (cmd === 'pickFolder') { r.value = 'C:\\Users\\you\\Projects'; delete r.state; }
    if (cmd === 'set') { const k = { studio: 'studioPath', location: 'location', prefix: 'prefix', accent: 'accent' }[a[0]]; mock.settings[k] = a[1]; r.state = JSON.parse(JSON.stringify(mock)); }
    if (cmd === 'create') {
      if (!a[0].trim()) { r.ok = false; r.error = 'Give your app a name.'; delete r.state; }
      else { mock.projects.push({ name: a[0], path: a[2] + '\\' + a[0].replace(/\s+/g, ''), pkg: a[1], created: Date.now()/1000, opened: 0, missing: false }); r.state = JSON.parse(JSON.stringify(mock)); r.toast = { text: 'Created ' + a[0], error: false }; }
    }
    res(r);
  }, 60));
}

/* ---------- state ---------- */
let S = { projects: [], settings: { studioPath: '', studioFound: false, location: '', prefix: 'com.example', accent: '#ff2d4d' }, dataPath: '' };
let view = [];
let animateNext = true;
let lastSig = '';

async function run(cmd, ...args) {
  const r = await call(cmd, ...args);
  if (r.state) { S = r.state; animateNext = true; render(); }
  if (r.toast) toast(r.toast.text, r.toast.error);
  return r;
}

/* ---------- helpers ---------- */
const TINTS = ['#ff2d4d', '#ff8a3d', '#f5c542', '#3ddc97', '#38bdf8', '#b18cff'];
function tintOf(name) { let h = 7; for (const c of name) h = (h * 31 + c.charCodeAt(0)) >>> 0; return TINTS[h % 6]; }
function initials(name) {
  const w = name.split(/[\s_\-.]+/).filter(Boolean);
  return (w.length >= 2 ? w[0][0] + w[1][0] : (w[0] || '?').slice(0, 2)).toUpperCase();
}
function ago(sec) {
  const s = Math.max(0, Date.now() / 1000 - sec);
  if (s < 60) return 'just now';
  if (s < 3600) return Math.floor(s / 60) + ' min ago';
  if (s < 86400) return Math.floor(s / 3600) + ' h ago';
  if (s < 172800) return 'yesterday';
  if (s < 86400 * 30) return Math.floor(s / 86400) + ' days ago';
  return new Date(sec * 1000).toLocaleDateString();
}
const stamp = p => p.opened || p.created;
const folderName = n => n.trim().replace(/[^\p{L}\p{N}_-]/gu, '');
const RESERVED = new Set('abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for goto if implements import instanceof int interface long native new package private protected public return short static strictfp super switch synchronized this throw throws transient try void volatile while true false null'.split(' '));
function suggestPackage(name) {
  let slug = name.toLowerCase().replace(/[^a-z0-9]/g, '');
  if (!slug) slug = 'myapp';
  if (/^\d/.test(slug)) slug = 'app' + slug;
  if (RESERVED.has(slug)) slug += 'app';
  return (S.settings.prefix || 'com.example') + '.' + slug;
}

/* ---------- toast ---------- */
let toastTimer;
function toast(text, error) {
  const t = $('#toast');
  $('#toastText').textContent = text;
  $('#toastIco').innerHTML = icon(error ? 'alert' : 'check');
  t.classList.toggle('err', !!error);
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 3600);
}

/* ---------- render ---------- */
function render() {
  document.documentElement.style.setProperty('--accent', S.settings.accent || '#ff2d4d');

  const total = S.projects.length;
  $('#count').textContent = total;
  $('#total').textContent = total;

  const found = S.settings.studioFound;
  $('#chip .dot').classList.toggle('ok', found);
  $('#chipText').textContent = found ? 'Android Studio ready' : 'Android Studio not found';

  renderList();
  renderSettings();
}

function renderList() {
  const q = $('#q').value.trim().toLowerCase();
  view = S.projects
    .filter(p => !q || p.name.toLowerCase().includes(q) || (p.pkg || '').toLowerCase().includes(q) || p.path.toLowerCase().includes(q))
    .sort((a, b) => stamp(b) - stamp(a));

  const list = $('#list'), empty = $('#empty');
  list.hidden = view.length === 0;
  empty.hidden = view.length !== 0;
  if (!view.length) {
    const none = S.projects.length === 0;
    $('#emptyTitle').textContent = none ? 'NO PROJECTS YET' : 'NOTHING MATCHES';
    $('#emptyText').textContent = none ? 'Create a starter project, or import one you already have.' : 'Try a different search.';
    $('#emptyBtns').hidden = !none;
    lastSig = '';
    return;
  }

  const html = view.map((p, i) => `
    <div class="proj${p.missing ? ' missing' : ''}" data-i="${i}" style="${animateNext ? `animation-delay:${Math.min(i, 10) * 25}ms` : 'animation:none'}">
      <div class="tile" style="--t:${tintOf(p.name)}">${esc(initials(p.name))}</div>
      <div class="pinfo">
        <div class="pname"><b>${esc(p.name)}</b>${p.missing ? '<span class="pill">FOLDER MISSING</span>' : ''}</div>
        <div class="ppath" title="${esc(p.path)}">${p.pkg ? esc(p.pkg) + '&nbsp;&nbsp;·&nbsp;&nbsp;' : ''}${esc(p.path)}</div>
      </div>
      <div class="pwhen">${p.opened ? 'Opened ' + ago(p.opened) : 'Created ' + ago(p.created)}</div>
      <div class="pact">
        <button class="ib" data-act="reveal" title="Show in folder"><span class="ico">${icon('folder')}</span></button>
        <button class="ib d" data-act="remove" title="Remove"><span class="ico">${icon('trash')}</span></button>
      </div>
      <span class="go ico">${icon('arrow')}</span>
    </div>`).join('');
  if (html !== lastSig) { list.innerHTML = html; lastSig = html; }
  animateNext = false;
}

function setVal(el, v) { if (document.activeElement !== el) el.value = v; }
function renderSettings() {
  const s = S.settings;
  setVal($('#sStudio'), s.studioPath);
  setVal($('#sLoc'), s.location);
  setVal($('#sPrefix'), s.prefix);
  $('#sDot').classList.toggle('ok', s.studioFound);
  $('#sStatus').textContent = s.studioFound ? 'Found. Projects will open with this install.' : 'Not found at this path.';
  $$('.sw-c').forEach(b => b.classList.toggle('on', b.dataset.c === (s.accent || '').toLowerCase()));
  $('#dataPath').textContent = S.dataPath ? 'Your project list is stored in ' + S.dataPath : '';
}

/* ---------- navigation ---------- */
function showPage(name) {
  $$('.nav').forEach(n => n.classList.toggle('on', n.dataset.page === name));
  $$('.page').forEach(p => p.classList.toggle('on', p.id === 'page-' + name));
}
$$('.nav').forEach(n => n.addEventListener('click', () => showPage(n.dataset.page)));
$('#chip').addEventListener('click', () => showPage('settings'));

/* ---------- project list ---------- */
$('#list').addEventListener('click', e => {
  const row = e.target.closest('.proj');
  if (!row) return;
  const p = view[+row.dataset.i];
  const btn = e.target.closest('.ib');
  if (btn) {
    e.stopPropagation();
    if (btn.dataset.act === 'reveal') call('reveal', p.path).then(r => r.toast && toast(r.toast.text, r.toast.error));
    else openConfirm(p);
    return;
  }
  run('open', p.path);
});
$('#q').addEventListener('input', () => renderList());
$('#q').addEventListener('keydown', e => { if (e.key === 'Escape') { e.target.value = ''; e.target.blur(); renderList(); } });
$('#importBtn').addEventListener('click', () => run('import'));
$('#emptyImport').addEventListener('click', () => run('import'));

/* ---------- remove / delete dialog ---------- */
let confirmProj = null;
function openConfirm(p) {
  confirmProj = p;
  $('#cTitle').textContent = 'REMOVE ' + p.name.toUpperCase() + '?';
  $('#confirmDlg').hidden = false;
}
const closeConfirm = () => { $('#confirmDlg').hidden = true; confirmProj = null; };
$('#cCancel').addEventListener('click', closeConfirm);
$('#cRemove').addEventListener('click', () => { const p = confirmProj; closeConfirm(); if (p) run('remove', p.path); });
$('#cDelete').addEventListener('click', () => { const p = confirmProj; closeConfirm(); if (p) run('delete', p.path); });
$('#confirmDlg').addEventListener('mousedown', e => { if (e.target.id === 'confirmDlg') closeConfirm(); });

/* ---------- new project dialog ---------- */
const dlg = { pkgTouched: false, lang: 'java', sdk: '26', wf: false, open: true };
function updatePreview() {
  const f = folderName($('#dName').value), loc = $('#dLoc').value.trim().replace(/[\\/]+$/, '');
  $('#dPreview').textContent = f && loc ? loc + '\\' + f : '';
}
function setErr(msg) { const e = $('#dErr'); e.textContent = msg || ''; e.hidden = !msg; }
function openNew() {
  $('#dName').value = '';
  $('#dPkg').value = suggestPackage('');
  $('#dLoc').value = S.settings.location;
  dlg.pkgTouched = false; dlg.wf = false; dlg.open = true;
  setSeg('#segLang', 'java'); setSeg('#segSdk', '26');
  $('#tgWf').classList.remove('on'); $('#tgOpen').classList.add('on');
  setErr(''); updatePreview();
  $('#newDlg').hidden = false;
  $('#dName').focus();
}
const closeNew = () => { $('#newDlg').hidden = true; };
function setSeg(sel, v) { $$(sel + ' button').forEach(b => b.classList.toggle('on', b.dataset.v === v)); }
$$('#segLang button').forEach(b => b.addEventListener('click', () => { dlg.lang = b.dataset.v; setSeg('#segLang', dlg.lang); }));
$$('#segSdk button').forEach(b => b.addEventListener('click', () => { dlg.sdk = b.dataset.v; setSeg('#segSdk', dlg.sdk); }));
$('#tgWf').addEventListener('click', e => { dlg.wf = e.currentTarget.classList.toggle('on'); });
$('#tgOpen').addEventListener('click', e => { dlg.open = e.currentTarget.classList.toggle('on'); });
$('#dName').addEventListener('input', () => {
  setErr('');
  if (!dlg.pkgTouched) $('#dPkg').value = suggestPackage($('#dName').value);
  updatePreview();
});
$('#dPkg').addEventListener('input', () => { dlg.pkgTouched = true; setErr(''); });
$('#dLoc').addEventListener('input', () => { setErr(''); updatePreview(); });
$('#dBrowse').addEventListener('click', async () => {
  const r = await call('pickFolder', $('#dLoc').value);
  if (r.value) { $('#dLoc').value = r.value; updatePreview(); }
});
async function submitNew() {
  const btn = $('#dCreate');
  btn.disabled = true;
  const r = await run('create', $('#dName').value, $('#dPkg').value, $('#dLoc').value,
                      dlg.lang, dlg.sdk, dlg.wf ? '1' : '0', dlg.open ? '1' : '0');
  btn.disabled = false;
  if (!r.ok) setErr(r.error); else closeNew();
}
$('#dCreate').addEventListener('click', submitNew);
$('#dCancel').addEventListener('click', closeNew);
$('#dClose').addEventListener('click', closeNew);
$('#newDlg').addEventListener('mousedown', e => { if (e.target.id === 'newDlg') closeNew(); });
['#dName', '#dPkg', '#dLoc'].forEach(s => $(s).addEventListener('keydown', e => { if (e.key === 'Enter') submitNew(); }));
$('#newBtn').addEventListener('click', openNew);
$('#emptyNew').addEventListener('click', openNew);

/* ---------- settings ---------- */
$('#sStudio').addEventListener('change', e => run('set', 'studio', e.target.value));
$('#sStudioBrowse').addEventListener('click', () => run('pickStudio'));
$('#sDetect').addEventListener('click', () => run('detect'));
$('#sLoc').addEventListener('change', e => run('set', 'location', e.target.value));
$('#sLocBrowse').addEventListener('click', async () => {
  const r = await call('pickFolder', $('#sLoc').value);
  if (r.value) run('set', 'location', r.value);
});
$('#sPrefix').addEventListener('change', e => run('set', 'prefix', e.target.value));
const COLORS = ['#ff2d4d', '#ff7a3d', '#f5c542', '#3ddc97', '#22d3ee', '#4c8dff', '#b18cff', '#ff5fb0'];
$('#swatches').innerHTML = COLORS.map(c => `<button class="sw-c" data-c="${c}" style="background:${c}" title="${c}"></button>`).join('');
$('#swatches').addEventListener('click', e => { const b = e.target.closest('.sw-c'); if (b) run('set', 'accent', b.dataset.c); });

/* ---------- global ---------- */
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') {
    if (!$('#confirmDlg').hidden) return closeConfirm();
    if (!$('#newDlg').hidden) return closeNew();
  }
  if (e.ctrlKey && e.key.toLowerCase() === 'n') { e.preventDefault(); openNew(); }
  if (e.ctrlKey && e.key.toLowerCase() === 'f') { e.preventDefault(); showPage('projects'); $('#q').focus(); $('#q').select(); }
});
window.addEventListener('focus', () => { animateNext = false; run('state'); });
document.addEventListener('contextmenu', e => { if (!/INPUT|TEXTAREA/.test(e.target.tagName)) e.preventDefault(); });

$$('.ico[data-i]').forEach(el => { el.innerHTML = icon(el.dataset.i); });

run('state');
setInterval(() => renderList(), 60000); // keep "x min ago" fresh
})();
