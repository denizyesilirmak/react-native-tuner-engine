export function centsColor(cents: number): string {
  const abs = Math.abs(cents);
  if (abs <= 5) return '#27ae60';
  if (abs <= 15) return '#f39c12';
  return '#c0392b';
}
