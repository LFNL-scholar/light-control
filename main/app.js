import { DAY, OFFSET, dayStart, dateKey, parseDate, periodRange, shiftPeriod, summarize, themeFor } from '/stats.js?v=2';
const el = id => document.getElementById(id);
let state = null;
let online = false;
let busy = false;

function canPower(s) {
  return Boolean(s && s.power_known && s.radio_ready && s.sample_loaded);
}

function enable() {
  el('light-on').disabled = busy || !online || !canPower(state) || state.power;
  el('light-off').disabled = busy || !online || !canPower(state) || !state.power;
  document.querySelectorAll('[data-action^="sync_"]').forEach(button => {
    button.disabled = busy || !online;
  });
}

function render(next) {
  state = next;
  online = true;
  el('dot').classList.add('online');
  el('connection').textContent = next.radio_ready && next.sample_loaded
    ? '设备在线 · 射频就绪'
    : '设备在线 · 射频待检查';

  if (!next.power_known) {
    el('power').textContent = '状态待校准';
  } else {
    el('power').textContent = next.power ? '推算：已开启' : '推算：已关闭';
  }

  if (!next.radio_ready) {
    el('rf-status').textContent = 'CC1101 未就绪，请检查接线与供电。';
  } else if (!next.sample_loaded) {
    el('rf-status').textContent = 'CC1101 正常，但未找到 power_sample_1。';
  } else {
    el('rf-status').textContent = `射频就绪 · 已发送 ${next.power_transmissions || 0} 次`;
  }

  window.dispatchEvent(new CustomEvent('lamp-state', { detail: next }));
  enable();
}

async function request(path, options) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 7000);
  try {
    const response = await fetch(path, {
      ...options,
      signal: controller.signal,
      cache: 'no-store'
    });
    const payload = await response.json().catch(() => null);
    if (payload && typeof payload.power_known === 'boolean') render(payload);
    if (!response.ok) {
      const error = new Error(payload?.last_error || `HTTP ${response.status}`);
      error.payload = payload;
      throw error;
    }
    return payload;
  } finally {
    clearTimeout(timer);
  }
}

function disconnected(message) {
  online = false;
  busy = false;
  el('dot').classList.remove('online');
  el('connection').textContent = '连接中断';
  el('message').textContent = message;
  enable();
}

async function refresh() {
  if (busy) return;
  try {
    const next = await request('/api/state');
    if (!next.power_known) {
      el('message').textContent = '请先按台灯实际状态完成一次校准。';
    } else {
      el('message').textContent = next.history_error ? '灯可控制 · 历史存储异常，请查看用灯记录。' : !next.time_synced ? '灯可控制 · 时间未同步，当前不计入统计。' : '状态正常 · 可控制台灯';
    }
  } catch (error) {
    if (error.payload) {
      el('message').textContent = `控制未完成：${error.message}`;
    } else {
      disconnected('无法连接 ESP32，请检查设备供电和 Wi-Fi。');
    }
  }
}

async function action(name) {
  if (busy || !online) return;
  busy = true;
  enable();
  if (name === 'on' || name === 'off') {
    el('message').textContent = name === 'on' ? '正在发送开灯命令…' : '正在发送关灯命令…';
  } else {
    el('message').textContent = '正在保存灯光状态…';
  }
  try {
    const next = await request('/api/action', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: name
    });
    refreshHistory(true);
    if (name.startsWith('sync_')) {
      el('message').textContent = '状态已校准并保存。';
    } else {
      const already = next.last_action.startsWith('already_');
      el('message').textContent = already ? '台灯已是目标状态，未重复发射。' : 'CC1101 已完成发送。';
    }
  } catch (error) {
    if (error.payload) {
      el('message').textContent = `控制未完成：${error.message}`;
    } else {
      disconnected('操作未确认，请检查连接后重新校准。');
    }
  } finally {
    busy = false;
    enable();
  }
}

document.querySelectorAll('[data-action]').forEach(button => {
  button.addEventListener('click', () => action(button.dataset.action));
});

refresh();
setInterval(refresh, 5000);

// The control API remains usable when WebGL or the graphics module fails.
import('/scene.js?v=2')
  .then(module => {
    module.startScene(el('scene'));
    if (state) window.dispatchEvent(new CustomEvent('lamp-state', { detail: state }));
  })
  .catch(error => {
    console.error(error);
    el('render-error').textContent = '3D 预览暂不可用，请使用支持 WebGL 的浏览器。开关控制仍可使用。';
  });

// Theme preference is per browser, independent of the device's power state.
let themePreference = 'auto';
let themeBoundaryTimer;
try { themePreference = localStorage.getItem('light-theme') || 'auto'; } catch {}
if (!['auto', 'light', 'dark'].includes(themePreference)) themePreference = 'auto';
function applyTheme() {
  const now = Date.now();
  const theme = themeFor(themePreference, now / 1000);
  document.documentElement.dataset.theme = theme;
  const automatic = themeFor('auto', now / 1000);
  const next = themePreference === 'auto' ? (theme === 'dark' ? 'light' : 'dark')
    : themePreference === automatic ? 'auto' : automatic;
  const names = { auto: '自动（早八晚六）', light: '浅色', dark: '深色' };
  const toggle = el('theme-toggle');
  toggle.dataset.auto = String(themePreference === 'auto');
  toggle.dataset.next = next;
  toggle.title = `当前：${names[themePreference]}；点击切换${names[next]}`;
  toggle.setAttribute('aria-label', toggle.title);
  document.querySelector('meta[name="theme-color"]').content = theme === 'dark' ? '#171c1b' : '#ece9e1';
  window.dispatchEvent(new CustomEvent('theme-change', { detail: theme }));
  clearTimeout(themeBoundaryTimer);
  if (themePreference === 'auto') {
    const midnight = dayStart(now / 1000) * 1000;
    const morning = midnight + 8 * 3600000, evening = midnight + 18 * 3600000;
    const next = now < morning ? morning : now < evening ? evening : morning + DAY * 1000;
    themeBoundaryTimer = setTimeout(applyTheme, next - now + 10);
  }
}
el('theme-toggle').addEventListener('click', () => {
  themePreference = el('theme-toggle').dataset.next;
  try { localStorage.setItem('light-theme', themePreference); } catch {}
  applyTheme();
});
window.addEventListener('storage', event => {
  if (event.key !== 'light-theme') return;
  themePreference = ['light', 'dark'].includes(event.newValue) ? event.newValue : 'auto';
  applyTheme();
});
applyTheme();
setInterval(applyTheme, 30000);
window.addEventListener('focus', applyTheme);

let period = 'day', historyGeneration = 0, historyPayload = null;
let lastHistoryFetch = 0, historyLoading = false;
el('history-date').value = dateKey(Date.now() / 1000);
const clockText = seconds => new Date((seconds + OFFSET) * 1000).toISOString().slice(11, 19);
const durationText = seconds => {
  if (seconds == null) return '—';
  if (seconds > 0 && seconds < 60) return '< 1 分钟';
  const minutes = Math.floor(seconds / 60);
  return minutes >= 60 ? `${Math.floor(minutes / 60)} 小时 ${minutes % 60} 分` : `${minutes} 分钟`;
};
function node(tag, className, text) {
  const result = document.createElement(tag);
  if (className) result.className = className;
  if (text != null) result.textContent = text;
  return result;
}
function historyNavigation() {
  const range = periodRange(el('history-date').value, period);
  if (!range) return null;
  const today = dayStart(Date.now() / 1000);
  const oldest = historyPayload?.oldest || parseDate(dateKey(today).replace(/^\d{4}/, y => String(+y - 1)).replace(/-02-29$/, '-02-28'));
  el('history-date').max = dateKey(today);
  el('history-date').min = dateKey(oldest);
  el('period-prev').disabled = range.from <= oldest;
  el('period-next').disabled = range.to > today;
  el('period-label').textContent = `${dateKey(range.from)}${period === 'day' ? '' : ` — ${dateKey(range.to - 1)}`} · 北京时间`;
  return range;
}
function renderHistory(payload, range) {
  historyPayload = payload;
  historyNavigation();
  const now = payload.now || Date.now() / 1000;
  const report = summarize(payload.records, range.from, range.to, now, payload.oldest);
  el('history-results').hidden = false;
  el('history-total').textContent = report.effectiveDays ? durationText(report.total) : '暂无记录';
  el('history-average').textContent = durationText(report.average);
  el('history-days').textContent = `${report.effectiveDays} 天`;
  const messages = [];
  if (!payload.ready) messages.push(`历史存储不可用（${payload.error || '请检查设备'}），当前不再记录`);
  if (payload.incomplete) messages.push('部分记录不完整，以下仅展示可读取数据');
  if (!payload.time_synced) messages.push('时间未同步：灯仍可控制，当前时段不计入统计');
  else if (state && !state.power_known) messages.push('重启后请校准灯的实际状态，再继续记录');
  else if (!payload.recording && payload.ready) messages.push('等待开始记录');
  if (!report.effectiveDays) messages.push('此周期暂无有效记录，未知时间不算作关灯');
  else messages.push(`共 ${report.effectiveDays} 天有有效记录${report.unknown > 0 ? ' · 存在未知时段，图中已标记' : ''}`);
  el('history-status').textContent = messages.join('。');
  el('history-status').classList.toggle('error', !payload.ready || payload.incomplete);
  const chart = el('history-chart'), events = el('history-events');
  chart.replaceChildren(); events.replaceChildren();
  if (period === 'day') {
    const day = report.days[0];
    chart.append(node('h3', '', '全天时间轴 · 空白为尚未发生的时间'));
    const timeline = node('div', 'timeline');
    timeline.setAttribute('role', 'img');
    timeline.setAttribute('aria-label', `推算开灯 ${durationText(day.on)}，未知 ${durationText(day.unknown)}`);
    for (const segment of day.segments) {
      const kind = segment.power === 1 ? 'on' : segment.power === 0 ? 'off' : segment.power === 'future' ? 'future' : 'unknown';
      const part = node('span', kind);
      part.style.width = `${(segment.end - segment.start) / DAY * 100}%`;
      part.title = `${clockText(segment.start)} — ${segment.end === day.end ? '24:00:00' : clockText(segment.end)} · ${{on:'推算开灯',off:'关灯',future:'尚未发生',unknown:'未知'}[kind]}`;
      timeline.append(part);
    }
    const ticks = node('div', 'time-ticks');
    for (const label of ['00:00', '06:00', '12:00', '18:00', '24:00']) ticks.append(node('span', '', label));
    chart.append(timeline, ticks);
    events.append(node('p', '', '开灯时段'));
    const lit = day.segments.filter(s => s.power === 1);
    if (!lit.length) events.append(node('p', '', day.known ? '已记录时段内没有开灯。' : '尚无可统计的开灯时段。'));
    for (const s of lit) {
      const row = node('div', 'event');
      row.append(node('time', '', `${clockText(s.start)} — ${s.end === day.end ? '24:00:00' : clockText(s.end)}`), node('span', '', '推算开灯'), node('small', '', durationText(s.end - s.start)));
      events.append(row);
    }
    events.append(node('p', '', '开关与校准记录'));
    if (!day.events.length) events.append(node('p', '', '当天暂无操作记录；跨日延续时段仍计入时长。'));
    for (const e of day.events) {
      const row = node('div', 'event');
      row.append(node('time', '', clockText(e.at)), node('span', '', e.cause === 2 ? `校准为${e.power ? '开灯' : '关灯'}` : e.cause === 3 ? `开始记录：${e.power ? '开灯' : '关灯'}` : e.power ? '网页开灯' : '网页关灯'), node('small', '', e.cause === 2 ? '仅从此刻生效' : e.cause === 3 ? '时间已同步' : '推算'));
      events.append(row);
    }
  } else {
    chart.append(node('h3', '', '每日开灯时长 · 满格为 24 小时 · 点击某天查看时段'));
    const scroll = node('div', 'chart-scroll'), bars = node('div', 'daily-bars');
    if (period === 'month') bars.style.minWidth = `${report.days.length * 29}px`;
    for (const day of report.days) {
      const item = node('button', 'bar-item');
      item.style.cssText = 'border:0;background:transparent;padding:0;color:inherit';
      const description = day.future ? '尚未发生' : day.expired ? '超出保留期限' : day.known ? `${durationText(day.on)}${day.unknown ? '，有未知时段' : ''}` : '无有效记录';
      item.title = `${dateKey(day.start)} · ${description}`;
      item.setAttribute('aria-label', item.title);
      item.disabled = day.future || day.expired;
      const track = node('span', `bar-track${day.unknown ? ' partial' : ''}`);
      const fill = node('span', 'bar-fill');
      fill.style.height = `${day.on / DAY * 100}%`;
      track.append(fill);
      item.append(track, node('span', 'bar-label', dateKey(day.start).slice(8)), node('span', 'bar-label', day.future ? '—' : day.known ? `${(day.on / 3600).toFixed(1)}h` : '?'));
      item.addEventListener('click', () => {
        el('history-date').value = dateKey(day.start);
        selectPeriod('day');
      });
      bars.append(item);
    }
    scroll.append(bars); chart.append(scroll);
  }
}
async function refreshHistory(force = false) {
  if (!force && (historyLoading || Date.now() - lastHistoryFetch < 30000)) return;
  const range = historyNavigation();
  if (!range) return;
  const generation = ++historyGeneration;
  historyLoading = true;
  el('history-card').setAttribute('aria-busy', 'true');
  el('history-status').textContent = '正在读取设备记录…';
  try {
    const payload = await request(`/api/history?from=${range.from}&to=${range.to}`);
    if (generation !== historyGeneration) return;
    if (!payload || !Array.isArray(payload.records)) throw new Error('记录格式无效');
    renderHistory(payload, range);
    lastHistoryFetch = Date.now();
  } catch (error) {
    if (generation !== historyGeneration) return;
    el('history-results').hidden = true;
    el('history-status').classList.add('error');
    el('history-status').textContent = '无法读取用灯记录，请检查连接后重试。灯光控制仍可独立使用。';
  } finally {
    if (generation === historyGeneration) {
      historyLoading = false;
      el('history-card').setAttribute('aria-busy', 'false');
    }
  }
}
function selectPeriod(next) {
  period = next;
  document.querySelectorAll('[data-period]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.period === next)));
  el('history-results').hidden = true;
  refreshHistory(true);
}
document.querySelectorAll('[data-period]').forEach(b => b.addEventListener('click', () => selectPeriod(b.dataset.period)));
el('history-date').addEventListener('change', () => {
  if (!el('history-date').validity.valid || !periodRange(el('history-date').value, period)) return;
  el('history-results').hidden = true;
  refreshHistory(true);
});
for (const [id, delta] of [['period-prev', -1], ['period-next', 1]]) {
  el(id).addEventListener('click', () => {
    el('history-date').value = shiftPeriod(el('history-date').value, period, delta);
    el('history-results').hidden = true;
    refreshHistory(true);
  });
}
el('period-today').addEventListener('click', () => {
  el('history-date').value = dateKey(Date.now() / 1000);
  el('history-results').hidden = true;
  refreshHistory(true);
});
el('history-refresh').addEventListener('click', () => refreshHistory(true));
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) { applyTheme(); refresh(); refreshHistory(true); }
});
refreshHistory(true);
setInterval(() => { if (!document.hidden) refreshHistory(); }, 30000);
