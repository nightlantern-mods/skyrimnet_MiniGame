// Turns a UTF-8 "key<TAB>text" list into a Skyrim interface translation file
// (Interface/Translations/<plugin>_<LANGUAGE>.txt: UTF-16 LE with BOM, CRLF), which MCM Helper
// uses for the $keys in config.json.
// usage: node tools/make_translation.js <input .tsv> <output .txt>
const fs = require("fs");
const path = require("path");

const [input, output] = process.argv.slice(2);
const lines = fs.readFileSync(input, "utf8").split(/\r?\n/).filter((l) => l.trim() !== "");
for (const line of lines) {
    if (!/^\$\S+\t\S/.test(line)) {
        throw new Error(`not "$key<TAB>text": ${line}`);
    }
}
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, "﻿" + lines.join("\r\n") + "\r\n", "utf16le");
console.log(`wrote ${lines.length} entries to ${output}`);
