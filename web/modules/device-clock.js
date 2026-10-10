let anchor = null;
export function formatDeviceClock(epoch, offset = 0) {
  if (!Number.isFinite(epoch) || epoch <= 1577836800 || epoch >= 4102444800 || !Number.isFinite(offset) || offset < -720 || offset > 840 || offset % 15) return null;
  const date = new Date((epoch + offset * 60) * 1000);
  return { time: date.toISOString().slice(11, 19), date: date.toISOString().slice(0, 10), zone: `UTC${offset < 0 ? '-' : '+'}${String(Math.floor(Math.abs(offset) / 60)).padStart(2, '0')}:${String(Math.abs(offset) % 60).padStart(2, '0')}` };
}
function paint() {
  const time = document.getElementById('headerClockTime');
  const date = document.getElementById('headerClockDate');
  if (!time || !date) return;
  const value = anchor && performance.now() - anchor.at < 30000 ? formatDeviceClock(anchor.epoch + (performance.now() - anchor.at) / 1000, anchor.offset) : null;
  time.textContent = value?.time || '--:--:--';
  date.textContent = value ? `${value.date} · ${value.zone}` : 'Waiting for time sync';
}
export function updateHeaderClock(status) {
  const epoch = Number(status.clock?.utc);
  anchor = status.clock?.synced && epoch > 1577836800 ? { epoch, offset: Number(status.device?.clockUtcOffsetMinutes || 0), at: performance.now() } : null;
  paint();
}
if (typeof document !== 'undefined') setInterval(paint, 1000);
