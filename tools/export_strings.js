// Writes the English strings.json (the file a translation overrides) from the texts built into
// view/index.html (STRINGS).
// usage: node tools/export_strings.js <output strings.json>
const fs = require("fs");
const path = require("path");

const html = fs.readFileSync(path.join(__dirname, "..", "view", "index.html"), "utf8");
const match = html.match(/const STRINGS = (\{[\s\S]*?\n        \});/);
if (!match) {
    throw new Error("STRINGS not found in view/index.html");
}
// a key written twice silently keeps only its last text
const keys = [...match[1].matchAll(/^\s*(\w+):/gm)].map((m) => m[1]);
const repeated = keys.filter((k, i) => keys.indexOf(k) !== i);
if (repeated.length) {
    throw new Error(`STRINGS has these keys more than once: ${[...new Set(repeated)].join(", ")}`);
}
const view = new Function(`return ${match[1]};`)();

const out = process.argv[2];
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, JSON.stringify({ view }, null, 2) + "\n", "utf8");
console.log(`wrote ${Object.keys(view).length} view strings to ${out}`);
