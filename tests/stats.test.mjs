import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const source = readFileSync(new URL('../main/stats.js', import.meta.url), 'utf8');
const { DAY, parseDate, dateKey, periodRange, shiftPeriod, summarize, themeFor } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const d = parseDate('2026-11-02');
test('Beijing calendar boundaries, Monday weeks, leap months and invalid dates', () => {
  assert.equal(dateKey(d), '2026-11-02');
  assert.equal(d, Date.parse('2026-11-01T16:00:00Z') / 1000);
  assert.deepEqual(periodRange('2026-11-08', 'week'), {from:d, to:d+7*DAY});
  assert.equal((periodRange('2028-02-20','month').to-periodRange('2028-02-20','month').from)/DAY,29);
  assert.ok(Number.isNaN(parseDate('2026-02-29')));
  assert.equal(shiftPeriod('2026-12-31','month',1),'2027-01-01');
  assert.equal(shiftPeriod('2026-11-02','week',-1),'2026-11-01');
});
test('automatic theme switches exactly at 08:00 and 18:00 Beijing; manual stays fixed', () => {
  assert.equal(themeFor('auto',d+8*3600-1),'dark');
  assert.equal(themeFor('auto',d+8*3600),'light');
  assert.equal(themeFor('auto',d+18*3600-1),'light');
  assert.equal(themeFor('auto',d+18*3600),'dark');
  assert.equal(themeFor('light',d),'light');
  assert.equal(themeFor('dark',d+12*3600),'dark');
});
test('cross-midnight records split between days and off records are valid days', () => {
  const r=summarize([[d+23*3600,d+DAY,1,1],[d+DAY,d+DAY+3600,1,0],[d+DAY+3600,d+2*DAY,0,1]],d,d+2*DAY,d+2*DAY);
  assert.equal(r.days[0].on,3600); assert.equal(r.days[1].on,3600);
  assert.equal(r.total,7200); assert.equal(r.average,3600); assert.equal(r.effectiveDays,2);
  const off=summarize([[d,d+DAY,0,2]],d,d+DAY,d+DAY);
  assert.equal(off.average,0); assert.equal(off.effectiveDays,1);
});
test('reboot gap is unknown; recovery duplicates and prefixes never double count', () => {
  const records=[[d,d+3600,1,2],[d,d+3600,1,2],[d,d+1800,1,2],[d+7200,d+10800,0,2]];
  const r=summarize(records,d,d+DAY,d+10800);
  assert.equal(r.total,3600); assert.equal(r.days[0].known,7200); assert.equal(r.unknown,3600);
  assert.equal(r.days[0].events.length,2);
});
test('calibration preserves earlier inference and creates a marker, no backdating', () => {
  const r=summarize([[d,d+3600,1,1],[d+3600,d+7200,0,2]],d,d+DAY,d+7200);
  assert.equal(r.total,3600); assert.equal(r.days[0].events[1].at,d+3600);
  assert.equal(r.days[0].events[1].cause,2);
});
test('unknown full days excluded from average; future and retention excluded from duration', () => {
  const r=summarize([[d,d+3600,1,1]],d,d+3*DAY,d+2*DAY);
  assert.equal(r.effectiveDays,1); assert.equal(r.average,3600); assert.equal(r.days[2].future,true);
  assert.equal(summarize([],d,d+DAY,d+DAY).average,null);
  assert.equal(summarize([[d,d+DAY,1,1]],d,d+DAY,d+DAY,d+DAY).total,0);
});
test('overlapping contradictory clock observations are unknown', () => {
  const r=summarize([[d,d+7200,1,1],[d+3600,d+10800,0,2]],d,d+DAY,d+10800);
  assert.equal(r.total,3600); assert.equal(r.unknown,3600); assert.equal(r.days[0].known,7200);
});
test('zero-duration calibration is visible and invalid records ignored', () => {
  const r=summarize([[d,d,1,2],[d,d-1,1,1],[d,d+3600,5,1]],d,d+DAY,d+1);
  assert.equal(r.total,0); assert.equal(r.days[0].events.length,1);
});
