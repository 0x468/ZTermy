'use strict';

const browserScheme = matchMedia('(prefers-color-scheme: dark)');
let draft = { mode: 'system', light: 'ztermy-light', dark: 'ztermy-dark', fixed: 'nord', accentMode: 'brand' };
let saved = structuredClone(draft), savedColors = {}, editing = false, hoverId = '', filter = 'all';
let slot = browserScheme.matches ? 'dark' : 'light';
const palette = id => palettes.find(p => p.id === id);
const systemMode = () => $('systemState').value === 'auto' ? (browserScheme.matches ? 'dark' : 'light') : $('systemState').value;
const effectiveId = config => config.mode === 'fixed' ? config.fixed : config[systemMode()];
const chosenId = () => draft.mode === 'fixed' ? draft.fixed : draft[slot];
const dirty = () => JSON.stringify(draft) !== JSON.stringify(saved) || JSON.stringify(overrides) !== JSON.stringify(savedColors);

function preview() {
  selected = palette(hoverId || (editing ? chosenId() : effectiveId(draft)));
  renderPreview();
  $('preview-status').textContent = hoverId
    ? `悬停 / 焦点预览 · ${selected.name} · 未改变草稿`
    : editing ? `草稿预览 · ${selected.name} · 应用后按主题模式生效` : `当前规则预览 · ${selected.name}`;
  $('description').textContent = '亮暗由主题决定，整套切换；悬停只影响右侧预览，点击选入草稿。';
  $('draft-status').textContent = dirty() ? '有未应用更改 · 应用仅保存在本页，不修改 ztermy' : '本页方案已应用 · 刷新前请导出保存';
  $('apply').disabled = !dirty();
  $('discard').disabled = !dirty() && !editing && !hoverId;
}

function thumbnail(p) {
  const t = tokens(p);
  return `<div class="mini-window" aria-hidden="true" style="--mini-bg:${t.background};--mini-fg:${t.foreground};--mini-chrome:${t.chrome};--mini-panel:${t.panel};--mini-accent:${t.accent}"><div class="mini-title"><i></i><span></span><b>− ×</b></div><div class="mini-body"><div class="mini-nav"><i></i><i></i><i></i></div><div class="mini-terminal"><i></i><i></i><i></i><em></em></div></div></div>`;
}

function renderLibrary() {
  const allowed = draft.mode === 'system' ? slot : filter;
  const visible = palettes.filter(p => allowed === 'all' || p.dark === (allowed === 'dark'));
  $('themes').innerHTML = visible.map(p => `<button class="theme" data-theme="${p.id}" aria-pressed="${p.id === chosenId()}">${thumbnail(p)}<span class="card-name">${esc(p.name)}<span class="card-check">${p.id === chosenId() ? '✓' : ''}</span></span><small>${p.dark ? '深色' : '浅色'} · 完整界面</small></button>`).join('');
  $('theme-count').textContent = `${visible.length} 套`;
  $('filters').hidden = draft.mode === 'system';
  $('library-title').textContent = draft.mode === 'system' ? `选择${slot === 'light' ? '浅色' : '深色'}主题` : '选择固定主题';
  $('library-hint').textContent = 'Tab 浏览卡片，Enter / 空格选中。';
  document.querySelectorAll('[data-filter]').forEach(b => b.setAttribute('aria-pressed', b.dataset.filter === filter));
}

function render() {
  hoverId = '';
  $('accentMode').value = draft.accentMode;
  $('slots').hidden = draft.mode !== 'system';
  $('systemState').disabled = false; // Simulate OS changes even while fixed, to show that fixed ignores them.
  $('light-name').textContent = palette(draft.light).name;
  $('dark-name').textContent = palette(draft.dark).name;
  document.querySelectorAll('[data-mode]').forEach(b => b.setAttribute('aria-pressed', b.dataset.mode === draft.mode));
  document.querySelectorAll('[data-slot]').forEach(b => {
    b.setAttribute('aria-pressed', b.dataset.slot === slot);
    const t = tokens(palette(draft[b.dataset.slot]));
    b.style.setProperty('--slot-bg', t.panel);
    b.style.setProperty('--slot-text', t.text);
    b.style.setProperty('--slot-accent', t.accent);
  });
  $('effective').textContent = `系统${systemMode() === 'light' ? '浅色' : '深色'} → ${palette(effectiveId(draft)).name}${draft.mode === 'fixed' ? '（固定，不跟随）' : ''}`;
  // A clicked draft colors the settings shell; merely hovering never changes it.
  const shell = tokens(palette(editing ? chosenId() : effectiveId(draft)));
  Object.entries(shell).forEach(([key, value]) => document.querySelector('.policy').style.setProperty('--' + key, value));
  renderLibrary();
  preview();
}

function peek(id) {
  if (editing && id === chosenId()) id = '';
  if (hoverId === id) return;
  hoverId = id;
  preview();
}
$('themes').onpointerover = e => { const card = e.target.closest('[data-theme]'); if (card) peek(card.dataset.theme); };
$('themes').onpointerleave = () => peek('');
$('themes').onfocusin = e => { const card = e.target.closest('[data-theme]'); if (card) peek(card.dataset.theme); };
$('themes').onfocusout = e => { if (!$('themes').contains(e.relatedTarget)) peek(''); };
$('themes').onclick = e => {
  const card = e.target.closest('[data-theme]');
  if (!card) return;
  draft[draft.mode === 'fixed' ? 'fixed' : slot] = card.dataset.theme;
  editing = true;
  // Preserve the focused card rather than rebuilding the grid after a click.
  hoverId = '';
  const shell = tokens(palette(chosenId()));
  Object.entries(shell).forEach(([key, value]) => document.querySelector('.policy').style.setProperty('--' + key, value));
  $('light-name').textContent = palette(draft.light).name;
  $('dark-name').textContent = palette(draft.dark).name;
  document.querySelectorAll('[data-slot]').forEach(b => {
    const t = tokens(palette(draft[b.dataset.slot]));
    b.style.setProperty('--slot-bg', t.panel);
    b.style.setProperty('--slot-text', t.text);
    b.style.setProperty('--slot-accent', t.accent);
  });
  document.querySelectorAll('[data-theme]').forEach(b => b.querySelector('.card-check').textContent = b.dataset.theme === chosenId() ? '✓' : '');
  $('effective').textContent = `系统${systemMode() === 'light' ? '浅色' : '深色'} → ${palette(effectiveId(draft)).name}${draft.mode === 'fixed' ? '（固定，不跟随）' : ''}`;
  preview();
};
$('modes').onclick = e => { if (e.target.dataset.mode) { draft.mode = e.target.dataset.mode; editing = true; render(); } };
$('slots').onclick = e => { const b = e.target.closest('[data-slot]'); if (b) { slot = b.dataset.slot; editing = true; render(); } };
$('filters').onclick = e => { if (e.target.dataset.filter) { filter = e.target.dataset.filter; render(); } };
$('systemState').onchange = () => { editing = false; if (draft.mode === 'system') slot = systemMode(); render(); };
browserScheme.addEventListener('change', () => { if ($('systemState').value === 'auto') { editing = false; slot = systemMode(); render(); } });
$('exitPreview').onclick = () => { editing = false; render(); };
$('apply').onclick = () => { saved = structuredClone(draft); savedColors = structuredClone(overrides); editing = false; render(); };
$('discard').onclick = () => {
  draft = structuredClone(saved);
  Object.keys(overrides).forEach(k => delete overrides[k]);
  Object.assign(overrides, structuredClone(savedColors));
  editing = false;
  slot = systemMode();
  render();
};
$('export').onclick = () => {
  const config = { format: 'ztermy-theme-proposal', version: 2, status: dirty() ? 'draft' : 'applied-in-preview', policy: structuredClone(draft), themes: {}, notes: structuredClone(notes) };
  const before = selected;
  for (const id of new Set([draft.light, draft.dark, draft.fixed])) {
    selected = palette(id);
    config.themes[id] = { ui: tokens(selected), terminal: terminalData() };
  }
  selected = before;
  download(config, 'ztermy-unified-theme-proposal.json');
};
render();
