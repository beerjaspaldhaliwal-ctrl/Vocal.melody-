// Inlines CSS and JS into single-file builds:
//   dist/melodylift.html  - standalone page, works offline from file://
//   dist/artifact.html    - body fragment for hosts that supply the skeleton
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (f) => readFileSync(path.join(root, f), 'utf8');
const html = read('index.html');
const between = (a, b) => html.slice(html.indexOf(a) + a.length, html.indexOf(b)).trim();

const css = read('css/style.css');
const js = ['src/dsp.js', 'src/notes.js', 'src/midi.js', 'src/app.js'].map(read).join('\n');
const safeJs = js.replace(/<\/script/gi, '<\\/script');

const head = between('<!-- HEAD:START -->', '<!-- HEAD:END -->')
  .replace('<link rel="stylesheet" href="css/style.css">', `<style>\n${css}\n</style>`);
const body = between('<!-- BODY:START -->', '<!-- BODY:END -->');
const script = `<script>\n${safeJs}\n</script>`;

mkdirSync(path.join(root, 'dist'), { recursive: true });
writeFileSync(
  path.join(root, 'dist', 'melodylift.html'),
  `<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n${head}\n</head>\n<body>\n${body}\n${script}\n</body>\n</html>\n`
);
writeFileSync(path.join(root, 'dist', 'artifact.html'), `${head}\n${body}\n${script}\n`);
console.log('built dist/melodylift.html and dist/artifact.html');
