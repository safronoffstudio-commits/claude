// Достаёт значение из JSON на stdin по пути вида result.username
let s = '';
process.stdin.on('data', (d) => (s += d)).on('end', () => {
  try {
    let v = JSON.parse(s);
    for (const k of (process.argv[2] || '').split('.')) {
      if (!k) continue;
      v = v == null ? undefined : v[k];
    }
    process.stdout.write(v === undefined || v === null ? '' : String(v));
  } catch {
    process.stdout.write('');
  }
});
