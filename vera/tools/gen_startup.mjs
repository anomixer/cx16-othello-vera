#!/usr/bin/env node
// Compile src/startup.bas into an Applesoft tokenized image (build/STARTUP).
// Boot chain: ProDOS -> BASIC.SYSTEM -> Applesoft STARTUP -> BRUN MAIN.BIN.
import fs from "fs";
import path from "path";
import { compileApplesoftBasic } from "./applebasic.mjs";

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/(\w:)/, "$1"));
const root = path.resolve(here, "..");
const build = path.join(root, "build");

const bytes = compileApplesoftBasic(path.join(root, "src"), "startup.bas");
fs.mkdirSync(build, { recursive: true });
fs.writeFileSync(path.join(build, "STARTUP"), Buffer.from(bytes));
console.log(`  build/STARTUP: ${bytes.length} bytes`);
